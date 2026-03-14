// RedisConPool.h

#pragma once
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>

// 前向声明，避免头文件直接依赖 hiredis
struct redisContext;

/**
 * RedisConPool
 * 线程安全的 Redis 连接池。
 * 使用方式：
 *   auto* ctx = pool.getConnection();
 *   if (ctx) { ... pool.returnConnection(ctx); }
 */
class RedisConPool {
public:
    RedisConPool(size_t poolSize,
        const std::string& host,
        int                port,
        const std::string& pwd);

    ~RedisConPool();

    // 取出一条连接（阻塞直到有可用连接或池关闭）
    // 返回 nullptr 表示池已关闭
    redisContext* getConnection();

    // 归还连接；传入损坏连接时自动重建以维持池容量
    void returnConnection(redisContext* ctx);

    // 关闭连接池，释放所有连接
    void Close();

    // 返回当前空闲连接数（仅供调试/监控）
    size_t availableCount() const;

private:
    // 创建并认证一条新连接，失败返回 nullptr
    redisContext* createAuthenticatedConnection();

    size_t      poolSize_;
    std::string host_;
    int         port_;
    std::string pwd_;

    std::atomic<bool>         b_stop_;
    std::queue<redisContext*>  connections_;
    mutable std::mutex         mutex_;  // mutable 允许 const 方法加锁
    std::condition_variable    cond_;
};