// StatusServiceImpl.cpp

#include "StatusServiceImpl.h"
#include "ConfigManager.h"
#include "global.h"
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <chrono>
#include <iostream>
#include <shared_mutex>
#include <stdexcept>
#include <thread>

namespace {
    constexpr auto kHeartbeatCheckInterval = std::chrono::seconds(1);
    constexpr auto kHeartbeatTimeout = std::chrono::seconds(10);

    std::string generateToken() {
        boost::uuids::uuid uuid = boost::uuids::random_generator()();
        return boost::uuids::to_string(uuid);
    }

    std::string buildServerId(const std::string& host, const std::string& port)
    {
        return host + ":" + port;
    }
} // namespace

// ──────────────────────────────────────────────
// 构造：从配置文件动态加载所有 ChatServer
// 同时为每台 ChatServer 补一个稳定的 server_id
// 初始状态全部视为离线，等待 Register/Heartbeat 置为在线
// ──────────────────────────────────────────────
StatusServiceImpl::StatusServiceImpl()
    : _server_index(0)
    , _stop_checker(false)
{
    auto& cfg = ConfigManager::getInstance();

    int count = 0;
    try {
        count = std::stoi(cfg["ChatServerCount"]["count"]);
    }
    catch (const std::exception& e) {
        std::cerr << "[StatusServiceImpl.cpp] StatusServiceImpl [构造] "
            << "读取 ChatServerCount 失败，将尝试加载 ChatServer1: " << e.what() << "\n";
        count = 1;
    }

    for (int i = 1; i <= count; ++i) {
        std::string section = "ChatServer" + std::to_string(i);
        std::string host = cfg[section]["host"];
        std::string port = cfg[section]["port"];

        if (host.empty() || port.empty()) {
            std::cerr << "[StatusServiceImpl.cpp] StatusServiceImpl [构造] "
                << section << " 的 Host 或 Port 为空，跳过\n";
            continue;
        }

        ChatServer server{ host, port, buildServerId(host, port) };
        _servers.push_back(server);
        _server_nodes[server.server_id] = ChatServerNode{
            server.host,
            server.port,
            server.server_id,
            false,
            std::chrono::steady_clock::time_point::min()
        };
        _server_users[server.server_id] = {};

        std::cout << "[StatusServiceImpl.cpp] StatusServiceImpl [构造] "
            << "已加载 " << section << " -> " << server.server_id << "，初始状态: offline\n";
    }

    if (_servers.empty()) {
        throw std::runtime_error(
            "[StatusServiceImpl.cpp] StatusServiceImpl [构造] "
            "没有任何可用的 ChatServer 配置，服务无法启动");
    }

    std::cout << "[StatusServiceImpl.cpp] StatusServiceImpl [构造] "
        << "共加载 " << _servers.size() << " 台 ChatServer\n";

    _checker_thread = std::thread(&StatusServiceImpl::HeartbeatCheckLoop, this);
}

StatusServiceImpl::~StatusServiceImpl()
{
    _stop_checker.store(true);
    if (_checker_thread.joinable()) {
        _checker_thread.join();
    }
}

// ──────────────────────────────────────────────
// GetChatServer：从“当前在线”的 ChatServer 中轮询分配
// 同时生成 token 并返回 server_id
// ──────────────────────────────────────────────
Status StatusServiceImpl::GetChatServer(ServerContext* context,
    const GetChatServerReq* request,
    GetChatServerRsp* reply)
{
    (void)context;

    std::vector<ChatServer> online_servers;
    {
        std::shared_lock<std::shared_mutex> lock(_server_mutex);
        for (const auto& server : _servers) {
            auto it = _server_nodes.find(server.server_id);
            if (it != _server_nodes.end() && it->second.online) {
                online_servers.push_back(server);
            }
        }
    }

    if (online_servers.empty()) {
        std::cerr << "[StatusServiceImpl.cpp] GetChatServer [GetChatServer] "
            << "当前没有在线的 ChatServer，uid: " << request->uid() << "\n";
        reply->set_error(ErrorCodes::RPC_Failed);
        return Status::OK;
    }

    std::size_t index = _server_index.fetch_add(1) % online_servers.size();
    const auto& server = online_servers[index];

    std::cout << "[StatusServiceImpl.cpp] GetChatServer [GetChatServer] "
        << "uid: " << request->uid()
        << " 分配到在线 ChatServer[" << index << "] "
        << server.server_id << "\n";

    reply->set_host(server.host);
    reply->set_port(server.port);
    reply->set_server_id(server.server_id);
    reply->set_token(generateToken());
    reply->set_error(ErrorCodes::Success);
    insertToken(request->uid(), reply->token());

    return Status::OK;
}

// ──────────────────────────────────────────────
// Login：供 ChatServer 校验客户端 token
// ──────────────────────────────────────────────
Status StatusServiceImpl::Login(ServerContext* context,
    const LoginReq* request,
    LoginRsp* reply)
{
    (void)context;
    const int uid = request->uid();
    const std::string token = request->token();

    if (token.empty()) {
        std::cerr << "[StatusServiceImpl.cpp] Login [Login] token 为空，uid: " << uid << "\n";
        reply->set_error(ErrorCodes::TokenInvalid);
        return Status::OK;
    }

    std::shared_lock<std::shared_mutex> lock(_token_mutex);
    if (!verifyTokenUnlocked(uid, token)) {
        if (_tokens.find(uid) == _tokens.end()) {
            std::cerr << "[StatusServiceImpl.cpp] Login [Login] uid 不存在: " << uid << "\n";
            reply->set_error(ErrorCodes::UidInvalid);
        }
        else {
            std::cerr << "[StatusServiceImpl.cpp] Login [Login] token 不匹配，uid: " << uid << "\n";
            reply->set_error(ErrorCodes::TokenInvalid);
        }
        return Status::OK;
    }

    std::cout << "[StatusServiceImpl.cpp] Login [Login] 登录成功，uid: " << uid << "\n";
    reply->set_error(ErrorCodes::Success);
    reply->set_uid(uid);
    reply->set_token(token);
    return Status::OK;
}

// ──────────────────────────────────────────────
// RegisterChatServer：ChatServer 启动后显式注册自己
// 注册成功后即视为在线，等待后续 Heartbeat 续期
// ──────────────────────────────────────────────
Status StatusServiceImpl::RegisterChatServer(ServerContext* context,
    const RegisterChatServerReq* request,
    RegisterChatServerRsp* reply)
{
    (void)context;
    upsertServerNode(request->server_id(), request->host(), request->port(), false);
    reply->set_error(ErrorCodes::Success);
    return Status::OK;
}

// ──────────────────────────────────────────────
// Heartbeat：ChatServer 周期性续期
// 收到心跳后刷新 last_heartbeat，并将节点置为在线
// ──────────────────────────────────────────────
Status StatusServiceImpl::Heartbeat(ServerContext* context,
    const HeartbeatReq* request,
    HeartbeatRsp* reply)
{
    (void)context;
    upsertServerNode(request->server_id(), request->host(), request->port(), true);
    reply->set_error(ErrorCodes::Success);
    reply->set_online(true);
    return Status::OK;
}

// ──────────────────────────────────────────────
// ReportUserOnline：ChatServer 登录成功后上报 uid 所在路由
// 这里复用 token 做一次身份校验，防止错误上报
// 同时把 uid 记入 server_id -> users 反向索引，便于节点超时后整体摘除
// ──────────────────────────────────────────────
Status StatusServiceImpl::ReportUserOnline(ServerContext* context,
    const ReportUserOnlineReq* request,
    ReportUserOnlineRsp* reply)
{
    (void)context;
    {
        std::shared_lock<std::shared_mutex> lock(_token_mutex);
        if (!verifyTokenUnlocked(request->uid(), request->token())) {
            std::cerr << "[StatusServiceImpl.cpp] ReportUserOnline [ReportUserOnline] token 无效，uid: "
                << request->uid() << "\n";
            reply->set_error(ErrorCodes::TokenInvalid);
            return Status::OK;
        }
    }

    ChatServer resolved = resolveServerIdentity(request->server_id(), request->host(), request->port());

    std::unique_lock<std::shared_mutex> server_lock(_server_mutex);
    auto server_it = _server_nodes.find(resolved.server_id);
    if (server_it == _server_nodes.end() || !server_it->second.online) {
        std::cerr << "[StatusServiceImpl.cpp] ReportUserOnline [ReportUserOnline] "
            << "ChatServer 未注册或不在线，uid: " << request->uid()
            << "，server_id: " << resolved.server_id << "\n";
        reply->set_error(ErrorCodes::RPC_Failed);
        return Status::OK;
    }

    UserRoute route;
    route.server_id = resolved.server_id;
    route.host = resolved.host;
    route.port = resolved.port;

    std::unique_lock<std::shared_mutex> route_lock(_route_mutex);
    auto old_route_it = _routes.find(request->uid());
    if (old_route_it != _routes.end() && old_route_it->second.server_id != route.server_id) {
        auto old_users_it = _server_users.find(old_route_it->second.server_id);
        if (old_users_it != _server_users.end()) {
            old_users_it->second.erase(request->uid());
        }
    }

    _routes.insert_or_assign(request->uid(), route);
    _server_users[route.server_id].insert(request->uid());

    std::cout << "[StatusServiceImpl.cpp] ReportUserOnline [ReportUserOnline] "
        << "uid: " << request->uid() << " 上线到 " << route.server_id << "\n";
    reply->set_error(ErrorCodes::Success);
    return Status::OK;
}

// ──────────────────────────────────────────────
// ReportUserOffline：ChatServer 连接断开后清理 uid 路由
// 只有 server_id 匹配时才移除，避免误删其他机器的新路由
// ──────────────────────────────────────────────
Status StatusServiceImpl::ReportUserOffline(ServerContext* context,
    const ReportUserOfflineReq* request,
    ReportUserOfflineRsp* reply)
{
    (void)context;

    ChatServer resolved = resolveServerIdentity(request->server_id(), "", "");

    std::unique_lock<std::shared_mutex> server_lock(_server_mutex);
    std::unique_lock<std::shared_mutex> route_lock(_route_mutex);

    auto it = _routes.find(request->uid());
    if (it != _routes.end()) {
        if (resolved.server_id.empty() || it->second.server_id == resolved.server_id) {
            std::cout << "[StatusServiceImpl.cpp] ReportUserOffline [ReportUserOffline] "
                << "uid: " << request->uid() << " 从 " << it->second.server_id << " 下线\n";
            auto users_it = _server_users.find(it->second.server_id);
            if (users_it != _server_users.end()) {
                users_it->second.erase(request->uid());
            }
            _routes.erase(it);
        }
    }

    reply->set_error(ErrorCodes::Success);
    return Status::OK;
}

// ──────────────────────────────────────────────
// QueryUserRoute：查询某个 uid 当前是否在线、在哪台 ChatServer
// 供后续好友申请和私聊跨服转发使用
// ──────────────────────────────────────────────
Status StatusServiceImpl::QueryUserRoute(ServerContext* context,
    const QueryUserRouteReq* request,
    QueryUserRouteRsp* reply)
{
    (void)context;
    std::shared_lock<std::shared_mutex> lock(_route_mutex);
    auto it = _routes.find(request->uid());
    if (it == _routes.end()) {
        std::cout << "[StatusServiceImpl.cpp] QueryUserRoute [QueryUserRoute] "
            << "uid: " << request->uid() << " 当前不在线\n";
        reply->set_error(ErrorCodes::Success);
        reply->set_online(false);
        return Status::OK;
    }

    std::cout << "[StatusServiceImpl.cpp] QueryUserRoute [QueryUserRoute] "
        << "uid: " << request->uid() << " 当前位于 " << it->second.server_id << "\n";
    reply->set_error(ErrorCodes::Success);
    reply->set_online(true);
    reply->set_server_id(it->second.server_id);
    reply->set_host(it->second.host);
    reply->set_port(it->second.port);
    return Status::OK;
}

void StatusServiceImpl::insertToken(int uid, const std::string& token)
{
    std::unique_lock<std::shared_mutex> lock(_token_mutex);
    auto [it, inserted] = _tokens.insert_or_assign(uid, token);
    if (inserted) {
        std::cout << "[StatusServiceImpl.cpp] insertToken [insertToken] 新增 token，uid: " << uid << "\n";
    }
    else {
        std::cout << "[StatusServiceImpl.cpp] insertToken [insertToken] 更新 token，uid: " << uid << "\n";
    }
}

bool StatusServiceImpl::verifyTokenUnlocked(int uid, const std::string& token) const
{
    const auto it = _tokens.find(uid);
    if (it == _tokens.end()) {
        return false;
    }
    return it->second == token;
}

ChatServer StatusServiceImpl::resolveServerIdentity(const std::string& server_id,
    const std::string& host,
    const std::string& port) const
{
    if (!server_id.empty()) {
        for (const auto& server : _servers) {
            if (server.server_id == server_id) {
                return server;
            }
        }
    }

    std::string effective_port = port;
    if (effective_port.empty() && !server_id.empty()) {
        size_t pos = server_id.rfind(':');
        if (pos != std::string::npos && pos + 1 < server_id.size()) {
            effective_port = server_id.substr(pos + 1);
        }
    }

    for (const auto& server : _servers) {
        if (!effective_port.empty() && server.port == effective_port) {
            return server;
        }
    }

    ChatServer resolved;
    resolved.host = host;
    resolved.port = effective_port.empty() ? port : effective_port;
    resolved.server_id = server_id.empty() ? buildServerId(host, resolved.port) : server_id;
    return resolved;
}

void StatusServiceImpl::upsertServerNode(const std::string& server_id,
    const std::string& host,
    const std::string& port,
    bool from_heartbeat)
{
    ChatServer resolved = resolveServerIdentity(server_id, host, port);
    const std::string real_server_id = resolved.server_id;
    if (real_server_id.empty()) {
        std::cerr << "[StatusServiceImpl.cpp] upsertServerNode [upsertServerNode] server_id 为空，忽略\n";
        return;
    }

    std::unique_lock<std::shared_mutex> lock(_server_mutex);
    auto& node = _server_nodes[real_server_id];
    const bool was_online = node.online;

    node.server_id = real_server_id;
    node.host = resolved.host;
    node.port = resolved.port;
    node.online = true;
    node.last_heartbeat = std::chrono::steady_clock::now();
    _server_users.try_emplace(real_server_id);

    if (!from_heartbeat) {
        std::cout << "[StatusServiceImpl.cpp] upsertServerNode [upsertServerNode] "
            << "收到注册，server_id: " << real_server_id
            << "，状态: " << (was_online ? "online" : "offline -> online") << "\n";
    }
}

void StatusServiceImpl::HeartbeatCheckLoop()
{
    while (!_stop_checker.load()) {
        std::this_thread::sleep_for(kHeartbeatCheckInterval);
        CleanupExpiredServers();
    }

    std::cout << "[StatusServiceImpl.cpp] HeartbeatCheckLoop [HeartbeatCheckLoop] 心跳检查线程退出\n";
}

void StatusServiceImpl::CleanupExpiredServers()
{
    const auto now = std::chrono::steady_clock::now();
    std::vector<std::string> expired_servers;

    {
        std::unique_lock<std::shared_mutex> server_lock(_server_mutex);
        for (auto& server_pair : _server_nodes) {
            auto& server_id = server_pair.first;
            auto& node = server_pair.second;
            if (!node.online) {
                continue;
            }

            if (node.last_heartbeat == std::chrono::steady_clock::time_point::min()) {
                continue;
            }

            if (now - node.last_heartbeat > kHeartbeatTimeout) {
                node.online = false;
                expired_servers.push_back(server_id);
                std::cerr << "[StatusServiceImpl.cpp] CleanupExpiredServers [CleanupExpiredServers] "
                    << "ChatServer 心跳超时，下线节点: " << server_id << "\n";
            }
        }

        if (expired_servers.empty()) {
            return;
        }

        std::unique_lock<std::shared_mutex> route_lock(_route_mutex);
        for (const auto& server_id : expired_servers) {
            auto users_it = _server_users.find(server_id);
            if (users_it == _server_users.end()) {
                continue;
            }

            for (int uid : users_it->second) {
                auto route_it = _routes.find(uid);
                if (route_it != _routes.end() && route_it->second.server_id == server_id) {
                    std::cout << "[StatusServiceImpl.cpp] CleanupExpiredServers [CleanupExpiredServers] "
                        << "摘除超时节点上的用户路由，uid: " << uid
                        << "，server_id: " << server_id << "\n";
                    _routes.erase(route_it);
                }
            }
            users_it->second.clear();
        }
    }
}
