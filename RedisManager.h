// RedisManager.h

#pragma once
#include "Singleton.h"
#include "RedisConPool.h"
#include <memory>
#include <string>
#include <string_view>

class RedisManager : public Singleton<RedisManager>
{
    friend class Singleton<RedisManager>;
public:
    ~RedisManager();

    // 初始化连接池（原来的 Connect + Auth 合并为一步）
    bool Init(const std::string& host, int port,
        const std::string& pwd, size_t poolSize = 8);

    bool Get(const std::string& key, std::string& value);
    bool Set(const std::string& key, const std::string& value);

    bool LPush(const std::string& key, const std::string& value);
    bool LPop(const std::string& key, std::string& value);
    bool RPush(const std::string& key, const std::string& value);
    bool RPop(const std::string& key, std::string& value);

    bool HSet(std::string_view key, std::string_view field, std::string_view value);
    bool HGet(std::string_view key, std::string_view field, std::string& value);

    bool Del(std::string_view key);
    bool ExistsKey(std::string_view key);

    void Close();

private:
    RedisManager() = default;
    RedisManager(const RedisManager&) = delete;
    RedisManager& operator=(const RedisManager&) = delete;

    std::unique_ptr<RedisConPool> pool_;
};