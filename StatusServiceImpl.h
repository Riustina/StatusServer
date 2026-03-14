// StatusServiceImpl.h

#pragma once
#include <grpcpp/grpcpp.h>
#include "message.grpc.pb.h"
#include <atomic>
#include <chrono>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using message::GetChatServerReq;
using message::GetChatServerRsp;
using message::HeartbeatReq;
using message::HeartbeatRsp;
using message::LoginReq;
using message::LoginRsp;
using message::QueryUserRouteReq;
using message::QueryUserRouteRsp;
using message::RegisterChatServerReq;
using message::RegisterChatServerRsp;
using message::ReportUserOfflineReq;
using message::ReportUserOfflineRsp;
using message::ReportUserOnlineReq;
using message::ReportUserOnlineRsp;
using message::StatusService;

struct ChatServer {
    std::string host;
    std::string port;
    std::string grpc_port;
    std::string server_id;
};

struct ChatServerNode {
    std::string host;
    std::string port;
    std::string grpc_port;
    std::string server_id;
    bool online;
    std::chrono::steady_clock::time_point last_heartbeat;
};

struct UserRoute {
    std::string server_id;
    std::string host;
    std::string port;
    std::string grpc_port;
};

class StatusServiceImpl final : public StatusService::Service
{
public:
    StatusServiceImpl();
    ~StatusServiceImpl();

    Status GetChatServer(ServerContext* context,
        const GetChatServerReq* request,
        GetChatServerRsp* reply) override;
    Status Login(ServerContext* context,
        const LoginReq* request,
        LoginRsp* reply) override;
    Status RegisterChatServer(ServerContext* context,
        const RegisterChatServerReq* request,
        RegisterChatServerRsp* reply) override;
    Status Heartbeat(ServerContext* context,
        const HeartbeatReq* request,
        HeartbeatRsp* reply) override;
    Status ReportUserOnline(ServerContext* context,
        const ReportUserOnlineReq* request,
        ReportUserOnlineRsp* reply) override;
    Status ReportUserOffline(ServerContext* context,
        const ReportUserOfflineReq* request,
        ReportUserOfflineRsp* reply) override;
    Status QueryUserRoute(ServerContext* context,
        const QueryUserRouteReq* request,
        QueryUserRouteRsp* reply) override;

private:
    void insertToken(int uid, const std::string& token);
    bool verifyTokenUnlocked(int uid, const std::string& token) const;
    ChatServer resolveServerIdentity(const std::string& server_id,
        const std::string& host,
        const std::string& port,
        const std::string& grpc_port) const;
    void upsertServerNode(const std::string& server_id,
        const std::string& host,
        const std::string& port,
        const std::string& grpc_port,
        bool from_heartbeat);
    void HeartbeatCheckLoop();
    void CleanupExpiredServers();

    std::vector<ChatServer>  _servers;
    std::atomic<std::size_t> _server_index;

    mutable std::shared_mutex _token_mutex;
    std::unordered_map<int, std::string> _tokens;

    mutable std::shared_mutex _server_mutex;
    std::unordered_map<std::string, ChatServerNode> _server_nodes;
    std::unordered_map<std::string, std::unordered_set<int>> _server_users;

    mutable std::shared_mutex _route_mutex;
    std::unordered_map<int, UserRoute> _routes;

    std::atomic<bool> _stop_checker;
    std::thread _checker_thread;
};