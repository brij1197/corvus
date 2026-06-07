#pragma once
#include <hiredis/hiredis.h>
#include <chrono>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace corvus::db
{

    struct RedisConfigError : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    struct RedisCommandError : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    struct RedisConfig
    {
        std::string host{"localhost"};
        int port{6379};
        int connect_timeout_ms{3000};
        int command_timeout_ms{1000};
        std::string password;
        int db{0};
    };

    class RedisReply
    {
    public:
        explicit RedisReply(redisReply *reply) : reply_(reply) {}
        ~RedisReply()
        {
            if (reply_)
                freeReplyObject(reply_);
        }

        redisReply *get() const noexcept { return reply_; }
        redisReply *operator->() const noexcept { return reply_; }
        explicit operator bool() const noexcept { return reply_ != nullptr; }

        bool is_nil() const noexcept { return !reply_ || reply_->type == REDIS_REPLY_NIL; }
        bool is_string() const noexcept { return !reply_ || reply_->type == REDIS_REPLY_STRING; }
        bool is_status() const noexcept { return !reply_ || reply_->type == REDIS_REPLY_STATUS; }
        bool is_integer() const noexcept { return !reply_ || reply_->type == REDIS_REPLY_INTEGER; }
        bool is_array() const noexcept { return !reply_ || reply_->type == REDIS_REPLY_ARRAY; }
        bool is_error() const noexcept { return !reply_ || reply_->type == REDIS_REPLY_ERROR; }

        std::string str() const;
        long long integer() const;

        RedisReply(const RedisReply &) = delete;
        RedisReply &operator=(const RedisReply &) = delete;

    private:
        redisReply *reply_;
    };

    class RedisConnection
    {
    public:
        explicit RedisConnection(const RedisConfig &config);
        ~RedisConnection();

        RedisConnection(const RedisConnection &) = delete;
        RedisConnection &operator=(const RedisConnection &) = delete;
        RedisConnection(RedisConnection &&other) noexcept;
        RedisConnection &operator=(RedisConnection &&other) noexcept;

        bool is_connected() const noexcept;

        void reconnect();

        void set(const std::string &key, const std::string &value,
                 int ttl_seconds = 0);

        std::string get(const std::string &key);

        long long del(const std::string &key);

        long long exists(const std::string &key);

        void expire(const std::string &key, int seconds);

        void hset(const std::string &key, const std::string &field,
                  const std::string &value);

        std::string hget(const std::string &key, const std::string &field);

        std::unordered_map<std::string, std::string>
        hgetall(const std::string &key);

        void hmset(const std::string &key,
                   const std::unordered_map<std::string, std::string> &fields);

        long long hdel(const std::string &key, const std::string &field);

        bool ping();

        const RedisConfig &config() const noexcept { return config_; }

        redisContext *ctx() noexcept { return ctx_; }

    private:
        RedisReply command(const char *fmt, ...);
        void check_connection();
        void apply_timeouts();
        void authenticate();
        void select_db();

        redisContext *ctx_{nullptr};
        RedisConfig config_;
    };

    RedisConfig redis_config_from_env();
} // namespace::db