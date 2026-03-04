#include "RedisConPool.h"

#include "RedisConPool.h"
#include <hiredis/hiredis.h>
#include <cstring>   // strcmp
#include <iostream>

#ifdef _WIN32
#include <winsock2.h>
#endif

// ──────────────────────────────────────────────────────────────
// 构造：建立 poolSize 条连接并全部认证
// ──────────────────────────────────────────────────────────────

RedisConPool::RedisConPool(size_t poolSize,
    const std::string& host,
    int                port,
    const std::string& pwd)
    : poolSize_(poolSize)
    , host_(host)
    , port_(port)
    , pwd_(pwd)
    , b_stop_(false)
{
    for (size_t i = 0; i < poolSize_; ++i) {
        redisContext* ctx = createAuthenticatedConnection();
        if (ctx != nullptr) {
            connections_.push(ctx);
        }
    }

    if (connections_.empty()) {
        std::cerr << "[RedisConPool.cpp] RedisConPool [构造] 警告：没有任何有效连接\n";
    }
    else {
        std::cout << "[RedisConPool.cpp] RedisConPool [构造] 初始化完成，有效连接数: "
            << connections_.size() << "/" << poolSize_ << "\n";
    }
}

// ──────────────────────────────────────────────────────────────
// 析构
// ──────────────────────────────────────────────────────────────

RedisConPool::~RedisConPool()
{
    Close();
}

// ──────────────────────────────────────────────────────────────
// getConnection：阻塞取连接
// ──────────────────────────────────────────────────────────────

redisContext* RedisConPool::getConnection()
{
    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait(lock, [this] {
        return b_stop_.load() || !connections_.empty();
        });

    if (b_stop_.load()) {
        return nullptr;
    }

    redisContext* ctx = connections_.front();
    connections_.pop();
    return ctx;
}

// ──────────────────────────────────────────────────────────────
// returnConnection：归还连接，损坏时自动重建
// ──────────────────────────────────────────────────────────────

void RedisConPool::returnConnection(redisContext* ctx)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (b_stop_.load()) {
        // 池已关闭，直接释放
        if (ctx != nullptr) {
            redisFree(ctx);
        }
        return;
    }

    if (ctx == nullptr || ctx->err != 0) {
        // 连接已损坏，尝试补充一条新连接维持池容量
        std::cerr << "[RedisConPool.cpp] returnConnection [归还] 检测到损坏的连接，尝试重建\n";
        if (ctx != nullptr) {
            redisFree(ctx);
        }
        redisContext* newCtx = createAuthenticatedConnection();
        if (newCtx != nullptr) {
            connections_.push(newCtx);
            cond_.notify_one();
        }
        return;
    }

    connections_.push(ctx);
    cond_.notify_one();
}

// ──────────────────────────────────────────────────────────────
// Close：停止连接池，释放所有连接
// ──────────────────────────────────────────────────────────────

void RedisConPool::Close()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (b_stop_.load()) return; // 防止重复关闭
        b_stop_.store(true);

        while (!connections_.empty()) {
            redisFree(connections_.front());
            connections_.pop();
        }
    }
    // 唤醒所有阻塞在 getConnection() 上的线程
    cond_.notify_all();
    std::cout << "[RedisConPool.cpp] Close [Close] 连接池已关闭，所有连接已释放\n";
}

// ──────────────────────────────────────────────────────────────
// availableCount：当前空闲连接数（调试/监控用）
// ──────────────────────────────────────────────────────────────

size_t RedisConPool::availableCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return connections_.size();
}

// ──────────────────────────────────────────────────────────────
// createAuthenticatedConnection（私有）：建立并认证单条连接
// ──────────────────────────────────────────────────────────────

redisContext* RedisConPool::createAuthenticatedConnection()
{
    struct timeval timeout = { 1, 500000 }; // 1.5 秒超时
    redisContext* ctx = redisConnectWithTimeout(host_.c_str(), port_, timeout);

    if (ctx == nullptr) {
        std::cerr << "[RedisConPool.cpp] createAuthenticatedConnection [连接] 内存分配失败\n";
        return nullptr;
    }
    if (ctx->err != 0) {
        std::cerr << "[RedisConPool.cpp] createAuthenticatedConnection [连接] 连接失败: "
            << ctx->errstr << "\n";
        redisFree(ctx);
        return nullptr;
    }

    // 使用 %b 避免密码中含空格/特殊字符被截断
    redisReply* reply = (redisReply*)redisCommand(ctx, "AUTH %b",
        pwd_.c_str(), (size_t)pwd_.length());

    if (reply == nullptr) {
        std::cerr << "[RedisConPool.cpp] createAuthenticatedConnection [AUTH] 命令无响应\n";
        redisFree(ctx);
        return nullptr;
    }

    bool authed = (reply->type == REDIS_REPLY_STATUS &&
        strcmp(reply->str, "OK") == 0);

    if (!authed) {
        std::cerr << "[RedisConPool.cpp] createAuthenticatedConnection [AUTH] 认证失败: "
            << (reply->str ? reply->str : "未知原因") << "\n";
        freeReplyObject(reply);
        redisFree(ctx);
        return nullptr;
    }

    freeReplyObject(reply);
    // std::cout << "[RedisConPool.cpp] createAuthenticatedConnection [AUTH] 认证成功\n";
    return ctx;
}