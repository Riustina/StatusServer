// StatusServiceImpl.cpp

#include "StatusServiceImpl.h"
#include "ConfigManager.h"
#include "global.h"
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <iostream>

namespace {
    std::string generateToken() {
        boost::uuids::uuid uuid = boost::uuids::random_generator()();
        return boost::uuids::to_string(uuid);
    }
} // namespace

// ──────────────────────────────────────────────
// 构造：从配置文件动态加载所有 ChatServer
// 配置文件格式约定：
//   [ChatServerCount]
//   count = 2
//   [ChatServer1]
//   Host = 127.0.0.1
//   Port = 8080
//   [ChatServer2]
//   ...
// ──────────────────────────────────────────────
StatusServiceImpl::StatusServiceImpl() : _server_index(0)
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

        _servers.push_back({ host, port });
        std::cout << "[StatusServiceImpl.cpp] StatusServiceImpl [构造] "
            << "已加载 " << section << " -> " << host << ":" << port << "\n";
    }

    if (_servers.empty()) {
        throw std::runtime_error(
            "[StatusServiceImpl.cpp] StatusServiceImpl [构造] "
            "没有任何可用的 ChatServer 配置，服务无法启动");
    }

    std::cout << "[StatusServiceImpl.cpp] StatusServiceImpl [构造] "
        << "共加载 " << _servers.size() << " 台 ChatServer\n";
}

// ──────────────────────────────────────────────
// GetChatServer：轮询分配 ChatServer
// ──────────────────────────────────────────────
Status StatusServiceImpl::GetChatServer(ServerContext* context,
    const GetChatServerReq* request,
    GetChatServerRsp* reply)
{
    // fetch_add 是原子操作，多线程下安全
    // 取模保证始终在合法范围内（_servers 构造后不再修改，无需加锁）
    std::size_t index = _server_index.fetch_add(1) % _servers.size();
    const auto& server = _servers[index];

    std::cout << "[StatusServiceImpl.cpp] GetChatServer [GetChatServer] "
        << "uid: " << request->uid()
        << " 分配到 ChatServer[" << index << "] "
        << server.host << ":" << server.port << "\n";

    reply->set_host(server.host);
    reply->set_port(server.port);
    reply->set_token(generateToken());
    reply->set_error(ErrorCodes::Success);

    return Status::OK;
}
