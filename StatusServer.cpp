// StatusServer.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。

#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include <boost/asio.hpp>
#include <grpcpp/grpcpp.h>

#include <json/json.h>
#include <json/value.h>
#include <json/reader.h>

#include "global.h"
#include "ConfigManager.h"
#include "RedisManager.h"
#include "MySqlMgr.h"
#include "AsioIOServicePool.h"
#include "StatusServiceImpl.h"

void RunServer()
{
    auto& cfg = ConfigManager::getInstance();
    const std::string host = cfg["StatusServer"]["host"];
    const std::string port = cfg["StatusServer"]["port"];

    if (host.empty() || port.empty()) {
        throw std::runtime_error("[main.cpp] RunServer: 配置文件中 StatusServer Host 或 Port 为空");
    }

    const std::string server_address = host + ":" + port;

    // 构建 gRPC 服务
    StatusServiceImpl service;
    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
    if (!server) {
        throw std::runtime_error("[main.cpp] RunServer: gRPC 服务器启动失败，地址: " + server_address);
    }
    std::cout << "[main.cpp] RunServer: gRPC 服务器已启动，监听地址: " << server_address << std::endl;

    // 用 io_context + signal_set 捕获退出信号
    boost::asio::io_context io_context;
    boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);

    signals.async_wait([&](const boost::system::error_code& ec, int sig) {
        if (ec) {
            std::cerr << "[main.cpp] RunServer: 信号等待出错: " << ec.message() << std::endl;
            return;
        }
        std::cout << "[main.cpp] RunServer: 收到信号 " << sig << "，正在优雅关闭..." << std::endl;
        server->Shutdown();
        io_context.stop();
        });

    // 单独线程跑 io_context，用 join 保证主线程退出前它已结束
    std::thread io_thread([&io_context]() {
        std::cout << "[main.cpp] RunServer: io_context 线程启动\n";
        io_context.run();
        std::cout << "[main.cpp] RunServer: io_context 线程退出\n";
        });

    // 阻塞直到 server->Shutdown() 被调用
    server->Wait();
    std::cout << "[main.cpp] RunServer: gRPC 服务器已关闭\n";

    // 确保 io_context 也已停止（防止信号之外的路径导致 Wait 提前返回）
    io_context.stop();
    io_thread.join();
}

int main(int argc, char** argv)
{
    try {
        RunServer();
    }
    catch (const std::exception& e) {
        std::cerr << "[main.cpp] main: 致命错误: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}