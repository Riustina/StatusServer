// StatusServiceImpl.h

#pragma once
#include <grpcpp/grpcpp.h>
#include "message.grpc.pb.h"
#include <atomic>
#include <string>
#include <vector>
#include <shared_mutex>

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using message::GetChatServerReq;
using message::GetChatServerRsp;
using message::LoginRsp;
using message::LoginReq;
using message::StatusService;

struct ChatServer {
    std::string host;
    std::string port;
};

class StatusServiceImpl final : public StatusService::Service
{
public:
    StatusServiceImpl();

    Status GetChatServer(ServerContext* context,
        const GetChatServerReq* request,
        GetChatServerRsp* reply) override;
    Status Login(ServerContext* context, const LoginReq* request,
        LoginRsp* reply);

private:
    void insertToken(int uid, const std::string& token);

    std::vector<ChatServer>  _servers;
    std::atomic<std::size_t> _server_index;
    mutable std::shared_mutex _token_mutex;
    std::unordered_map<int, std::string> _tokens;
};

