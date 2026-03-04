// RedisManager.cpp

#include "RedisManager.h"
#include <hiredis/hiredis.h>
#include <cstring>   // strcmp
#include <iostream>

#ifdef _WIN32
#include <winsock2.h>
#endif

// ──────────────────────────────────────────────────────────────
// RAII 守卫：自动将连接归还连接池
// 好处：函数中途 return / 抛异常都能保证连接被归还，不会泄漏
// ──────────────────────────────────────────────────────────────
struct ConnectionGuard {
    RedisConPool* pool;
    redisContext* ctx;

    ConnectionGuard(RedisConPool* p, redisContext* c) : pool(p), ctx(c) {}
    ~ConnectionGuard() {
        if (pool && ctx) {
            pool->returnConnection(ctx);
        }
    }

    ConnectionGuard(const ConnectionGuard&) = delete;
    ConnectionGuard& operator=(const ConnectionGuard&) = delete;
};

// ──────────────────────────────────────────────────────────────
// 构造 / 析构
// ──────────────────────────────────────────────────────────────

RedisManager::~RedisManager() { Close(); }

// ──────────────────────────────────────────────────────────────
// Init / Close
// ──────────────────────────────────────────────────────────────

bool RedisManager::Init(const std::string& host, int port,
    const std::string& pwd, size_t poolSize)
{
    pool_ = std::make_unique<RedisConPool>(poolSize, host, port, pwd);
    if (pool_->availableCount() == 0) {
        std::cerr << "[RedisManager.cpp] Init [Init] 连接池初始化失败，没有可用连接\n";
        pool_.reset();
        return false;
    }
    std::cout << "[RedisManager.cpp] Init [Init] 连接池就绪，可用连接数: "
        << pool_->availableCount() << "\n";
    return true;
}

void RedisManager::Close()
{
    if (pool_) {
        pool_->Close();
        pool_.reset();
        std::cout << "[RedisManager.cpp] Close [Close] 连接池已关闭\n";
    }
}

// ──────────────────────────────────────────────────────────────
// SET / GET
// ──────────────────────────────────────────────────────────────

bool RedisManager::Set(const std::string& key, const std::string& value)
{
    if (!pool_) return false;
    ConnectionGuard g(pool_.get(), pool_->getConnection());
    if (!g.ctx) { std::cerr << "[RedisManager.cpp] Set [SET] 获取连接失败\n"; return false; }

    redisReply* reply = (redisReply*)redisCommand(g.ctx, "SET %b %b",
        key.c_str(), (size_t)key.length(), value.c_str(), (size_t)value.length());
    if (!reply) { std::cerr << "[RedisManager.cpp] Set [SET] 命令无响应，Key: " << key << "\n"; g.ctx->err = 1; return false; }

    bool ok = false;
    if (reply->type == REDIS_REPLY_STATUS && strcmp(reply->str, "OK") == 0) {
        std::cout << "[RedisManager.cpp] Set [SET] 成功，Key: " << key << "\n"; ok = true;
    }
    else if (reply->type == REDIS_REPLY_ERROR) {
        std::cerr << "[RedisManager.cpp] Set [SET] Redis 返回错误: " << reply->str << "\n";
    }
    else {
        std::cerr << "[RedisManager.cpp] Set [SET] 未预期的返回类型: " << reply->type << "\n";
    }
    freeReplyObject(reply);
    return ok;
}

bool RedisManager::Get(const std::string& key, std::string& value)
{
    if (!pool_) return false;
    ConnectionGuard g(pool_.get(), pool_->getConnection());
    if (!g.ctx) { std::cerr << "[RedisManager.cpp] Get [GET] 获取连接失败\n"; return false; }

    redisReply* reply = (redisReply*)redisCommand(g.ctx, "GET %b", key.c_str(), (size_t)key.length());
    if (!reply) { std::cerr << "[RedisManager.cpp] Get [GET] 命令无响应，Key: " << key << "\n"; g.ctx->err = 1; return false; }

    bool ok = false;
    switch (reply->type) {
    case REDIS_REPLY_STRING:
        value.assign(reply->str, reply->len);
        std::cout << "[RedisManager.cpp] Get [GET] 成功，Key: " << key << "\n"; ok = true; break;
    case REDIS_REPLY_NIL:
        std::cout << "[RedisManager.cpp] Get [GET] Key 不存在: " << key << "\n"; value.clear(); break;
    case REDIS_REPLY_ERROR:
        std::cerr << "[RedisManager.cpp] Get [GET] Redis 返回错误: " << reply->str << "\n"; break;
    default:
        std::cerr << "[RedisManager.cpp] Get [GET] 未预期的返回类型: " << reply->type << "\n";
    }
    freeReplyObject(reply);
    return ok;
}

// ──────────────────────────────────────────────────────────────
// LPUSH / LPOP / RPUSH / RPOP
// ──────────────────────────────────────────────────────────────

bool RedisManager::LPush(const std::string& key, const std::string& value)
{
    if (!pool_) return false;
    ConnectionGuard g(pool_.get(), pool_->getConnection());
    if (!g.ctx) { std::cerr << "[RedisManager.cpp] LPush [LPUSH] 获取连接失败\n"; return false; }

    redisReply* reply = (redisReply*)redisCommand(g.ctx, "LPUSH %b %b",
        key.c_str(), (size_t)key.length(), value.c_str(), (size_t)value.length());
    if (!reply) { std::cerr << "[RedisManager.cpp] LPush [LPUSH] 命令无响应，Key: " << key << "\n"; g.ctx->err = 1; return false; }

    bool ok = false;
    if (reply->type == REDIS_REPLY_INTEGER) {
        std::cout << "[RedisManager.cpp] LPush [LPUSH] 成功，Key: " << key << "，当前列表长度: " << reply->integer << "\n"; ok = true;
    }
    else if (reply->type == REDIS_REPLY_ERROR) {
        std::cerr << "[RedisManager.cpp] LPush [LPUSH] Redis 返回错误: " << reply->str << "\n";
    }
    else {
        std::cerr << "[RedisManager.cpp] LPush [LPUSH] 未预期的返回类型: " << reply->type << "\n";
    }
    freeReplyObject(reply);
    return ok;
}

bool RedisManager::LPop(const std::string& key, std::string& value)
{
    if (!pool_) return false;
    ConnectionGuard g(pool_.get(), pool_->getConnection());
    if (!g.ctx) { std::cerr << "[RedisManager.cpp] LPop [LPOP] 获取连接失败\n"; return false; }

    redisReply* reply = (redisReply*)redisCommand(g.ctx, "LPOP %b", key.c_str(), (size_t)key.length());
    if (!reply) { std::cerr << "[RedisManager.cpp] LPop [LPOP] 命令无响应，Key: " << key << "\n"; g.ctx->err = 1; return false; }

    bool ok = false;
    switch (reply->type) {
    case REDIS_REPLY_STRING:
        value.assign(reply->str, reply->len);
        std::cout << "[RedisManager.cpp] LPop [LPOP] 成功，Key: " << key << "\n"; ok = true; break;
    case REDIS_REPLY_NIL:
        std::cout << "[RedisManager.cpp] LPop [LPOP] 队列为空，Key: " << key << "\n"; break;
    case REDIS_REPLY_ERROR:
        std::cerr << "[RedisManager.cpp] LPop [LPOP] Redis 返回错误: " << reply->str << "\n"; break;
    default:
        std::cerr << "[RedisManager.cpp] LPop [LPOP] 未预期的返回类型: " << reply->type << "\n";
    }
    freeReplyObject(reply);
    return ok;
}

bool RedisManager::RPush(const std::string& key, const std::string& value)
{
    if (!pool_) return false;
    ConnectionGuard g(pool_.get(), pool_->getConnection());
    if (!g.ctx) { std::cerr << "[RedisManager.cpp] RPush [RPUSH] 获取连接失败\n"; return false; }

    redisReply* reply = (redisReply*)redisCommand(g.ctx, "RPUSH %b %b",
        key.c_str(), (size_t)key.length(), value.c_str(), (size_t)value.length());
    if (!reply) { std::cerr << "[RedisManager.cpp] RPush [RPUSH] 命令无响应，Key: " << key << "\n"; g.ctx->err = 1; return false; }

    bool ok = false;
    if (reply->type == REDIS_REPLY_INTEGER) {
        std::cout << "[RedisManager.cpp] RPush [RPUSH] 成功，Key: " << key << "，当前列表长度: " << reply->integer << "\n"; ok = true;
    }
    else if (reply->type == REDIS_REPLY_ERROR) {
        std::cerr << "[RedisManager.cpp] RPush [RPUSH] Redis 返回错误: " << reply->str << "\n";
    }
    else {
        std::cerr << "[RedisManager.cpp] RPush [RPUSH] 未预期的返回类型: " << reply->type << "\n";
    }
    freeReplyObject(reply);
    return ok;
}

bool RedisManager::RPop(const std::string& key, std::string& value)
{
    if (!pool_) return false;
    ConnectionGuard g(pool_.get(), pool_->getConnection());
    if (!g.ctx) { std::cerr << "[RedisManager.cpp] RPop [RPOP] 获取连接失败\n"; return false; }

    redisReply* reply = (redisReply*)redisCommand(g.ctx, "RPOP %b", key.c_str(), (size_t)key.length());
    if (!reply) { std::cerr << "[RedisManager.cpp] RPop [RPOP] 命令无响应，Key: " << key << "\n"; g.ctx->err = 1; return false; }

    bool ok = false;
    switch (reply->type) {
    case REDIS_REPLY_STRING:
        value.assign(reply->str, reply->len);
        std::cout << "[RedisManager.cpp] RPop [RPOP] 成功，Key: " << key << "\n"; ok = true; break;
    case REDIS_REPLY_NIL:
        std::cout << "[RedisManager.cpp] RPop [RPOP] 队列为空，Key: " << key << "\n"; break;
    case REDIS_REPLY_ERROR:
        std::cerr << "[RedisManager.cpp] RPop [RPOP] Redis 返回错误: " << reply->str << "\n"; break;
    default:
        std::cerr << "[RedisManager.cpp] RPop [RPOP] 未预期的返回类型: " << reply->type << "\n";
    }
    freeReplyObject(reply);
    return ok;
}

// ──────────────────────────────────────────────────────────────
// HSET / HGET
// ──────────────────────────────────────────────────────────────

bool RedisManager::HSet(std::string_view key, std::string_view field, std::string_view value)
{
    if (!pool_) return false;
    ConnectionGuard g(pool_.get(), pool_->getConnection());
    if (!g.ctx) { std::cerr << "[RedisManager.cpp] HSet [HSET] 获取连接失败\n"; return false; }

    redisReply* reply = (redisReply*)redisCommand(g.ctx, "HSET %b %b %b",
        key.data(), key.size(), field.data(), field.size(), value.data(), value.size());
    if (!reply) { std::cerr << "[RedisManager.cpp] HSet [HSET] 命令无响应，Key: " << key << "\n"; g.ctx->err = 1; return false; }

    bool ok = false;
    if (reply->type == REDIS_REPLY_INTEGER) {
        std::cout << "[RedisManager.cpp] HSet [HSET] 成功，Key: " << key << "，Field: " << field
            << (reply->integer == 1 ? "（新增）" : "（更新）") << "\n"; ok = true;
    }
    else if (reply->type == REDIS_REPLY_ERROR) {
        std::cerr << "[RedisManager.cpp] HSet [HSET] Redis 返回错误: " << reply->str << "\n";
    }
    else {
        std::cerr << "[RedisManager.cpp] HSet [HSET] 未预期的返回类型: " << reply->type << "\n";
    }
    freeReplyObject(reply);
    return ok;
}

bool RedisManager::HGet(std::string_view key, std::string_view field, std::string& value)
{
    if (!pool_) return false;
    ConnectionGuard g(pool_.get(), pool_->getConnection());
    if (!g.ctx) { std::cerr << "[RedisManager.cpp] HGet [HGET] 获取连接失败\n"; return false; }

    redisReply* reply = (redisReply*)redisCommand(g.ctx, "HGET %b %b",
        key.data(), key.size(), field.data(), field.size());
    if (!reply) { std::cerr << "[RedisManager.cpp] HGet [HGET] 命令无响应，Key: " << key << "\n"; g.ctx->err = 1; return false; }

    bool ok = false;
    switch (reply->type) {
    case REDIS_REPLY_STRING:
        value.assign(reply->str, reply->len);
        std::cout << "[RedisManager.cpp] HGet [HGET] 成功，Key: " << key << "，Field: " << field << "\n"; ok = true; break;
    case REDIS_REPLY_NIL:
        std::cout << "[RedisManager.cpp] HGet [HGET] Field 不存在，Key: " << key << "，Field: " << field << "\n"; break;
    case REDIS_REPLY_ERROR:
        std::cerr << "[RedisManager.cpp] HGet [HGET] Redis 返回错误: " << reply->str << "\n"; break;
    default:
        std::cerr << "[RedisManager.cpp] HGet [HGET] 未预期的返回类型: " << reply->type << "\n";
    }
    freeReplyObject(reply);
    return ok;
}

// ──────────────────────────────────────────────────────────────
// DEL / EXISTS
// ──────────────────────────────────────────────────────────────

bool RedisManager::Del(std::string_view key)
{
    if (!pool_) return false;
    ConnectionGuard g(pool_.get(), pool_->getConnection());
    if (!g.ctx) { std::cerr << "[RedisManager.cpp] Del [DEL] 获取连接失败\n"; return false; }

    redisReply* reply = (redisReply*)redisCommand(g.ctx, "DEL %b", key.data(), key.size());
    if (!reply) { std::cerr << "[RedisManager.cpp] Del [DEL] 命令无响应，Key: " << key << "\n"; g.ctx->err = 1; return false; }

    bool ok = false;
    if (reply->type == REDIS_REPLY_INTEGER) {
        if (reply->integer > 0) std::cout << "[RedisManager.cpp] Del [DEL] 成功删除，Key: " << key << "\n";
        else                    std::cout << "[RedisManager.cpp] Del [DEL] Key 不存在（无需删除），Key: " << key << "\n";
        ok = true;
    }
    else if (reply->type == REDIS_REPLY_ERROR) {
        std::cerr << "[RedisManager.cpp] Del [DEL] Redis 返回错误: " << reply->str << "\n";
    }
    else {
        std::cerr << "[RedisManager.cpp] Del [DEL] 未预期的返回类型: " << reply->type << "\n";
    }
    freeReplyObject(reply);
    return ok;
}

bool RedisManager::ExistsKey(std::string_view key)
{
    if (!pool_) return false;
    ConnectionGuard g(pool_.get(), pool_->getConnection());
    if (!g.ctx) { std::cerr << "[RedisManager.cpp] ExistsKey [EXISTS] 获取连接失败\n"; return false; }

    redisReply* reply = (redisReply*)redisCommand(g.ctx, "EXISTS %b", key.data(), key.size());
    if (!reply) { std::cerr << "[RedisManager.cpp] ExistsKey [EXISTS] 命令无响应，Key: " << key << "\n"; g.ctx->err = 1; return false; }

    bool found = false;
    if (reply->type == REDIS_REPLY_INTEGER) {
        found = (reply->integer > 0);
        if (found) std::cout << "[RedisManager.cpp] ExistsKey [EXISTS] Key 存在: " << key << "\n";
        else       std::cout << "[RedisManager.cpp] ExistsKey [EXISTS] Key 不存在: " << key << "\n";
    }
    else if (reply->type == REDIS_REPLY_ERROR) {
        std::cerr << "[RedisManager.cpp] ExistsKey [EXISTS] Redis 返回错误: " << reply->str << "\n";
    }
    else {
        std::cerr << "[RedisManager.cpp] ExistsKey [EXISTS] 未预期的返回类型: " << reply->type << "\n";
    }
    freeReplyObject(reply);
    return found;
}