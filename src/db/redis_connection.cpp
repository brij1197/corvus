#include "corvus/db/redis_connection.h"
#include <cstdarg>
#include <cstdlib>
#include <stdexcept>

namespace corvus::db
{

    std::string RedisReply::str() const
    {
        if (!reply_ || reply_->type == REDIS_REPLY_NIL)
            return "";
        if (reply_->type != REDIS_REPLY_STRING &&
            reply_->type != REDIS_REPLY_STATUS)
            throw RedisCommandError("Reply is not a string");
        return std::string(reply_->str, reply_->len);
    }

    long long RedisReply::integer() const
    {
        if (!reply_ || reply_->type != REDIS_REPLY_INTEGER)
            throw RedisCommandError("Reply is not an integer");
        return reply_->integer;
    }

    RedisConnection::RedisConnection(const RedisConfig &config) : config_(config)
    {
        reconnect();
    }

    RedisConnection::~RedisConnection()
    {
        if (ctx_)
        {
            redisFree(ctx_);
            ctx_ = nullptr;
        }
    }

    RedisConnection::RedisConnection(RedisConnection &&other) noexcept
        : ctx_(other.ctx_), config_(std::move(other.config_))
    {
        other.ctx_ = nullptr;
    }

    RedisConnection &RedisConnection::operator=(RedisConnection &&other) noexcept
    {
        if (this != &other)
        {
            if (ctx_)
            {
                redisFree(ctx_);
            }
            ctx_ = other.ctx_;
            config_ = std::move(other.config_);
            other.ctx_ = nullptr;
        }
        return *this;
    }

    bool RedisConnection::is_connected() const noexcept
    {
        return ctx_ != nullptr && ctx_->err == 0;
    }

    void RedisConnection::reconnect()
    {
        if (ctx_)
        {
            redisFree(ctx_);
            ctx_ = nullptr;
        }

        struct timeval tv;
        tv.tv_sec = config_.connect_timeout_ms / 1000;
        tv.tv_usec = (config_.connect_timeout_ms % 1000) * 1000;

        ctx_ = redisConnectWithTimeout(
            config_.host.c_str(), config_.port, tv);

        if (!ctx_)
            throw RedisConfigError(
                "Failed to allocate Redis context for " +
                config_.host + ":" + std::to_string(config_.port));

        if (ctx_->err)
        {
            std::string msg = "Redis connection failed to" +
                              config_.host + ":" +
                              std::to_string(config_.port) + ": " +
                              ctx_->errstr;
            redisFree(ctx_);
            ctx_ = nullptr;
            throw RedisConfigError(msg);
        }

        apply_timeouts();
        authenticate();
        select_db();
    }
    void RedisConnection::apply_timeouts()
    {
        struct timeval tv;
        tv.tv_sec = config_.command_timeout_ms / 1000;
        tv.tv_usec = (config_.command_timeout_ms % 1000) * 1000;

        redisSetTimeout(ctx_, tv);
    }

    void RedisConnection::authenticate()
    {
        if (config_.password.empty())
            return;

        auto *r = static_cast<redisReply *>(
            redisCommand(ctx_, "AUTH %s", config_.password.c_str()));
        RedisReply reply(r);

        if (!reply || reply.is_error())
            throw RedisConfigError(
                "Redis AUTH failed: " +
                (reply ? std::string(reply->str) : "null reply"));
    }

    void RedisConnection::select_db()
    {
        if (config_.db == 0)
            return;

        auto *r = static_cast<redisReply *>(redisCommand(ctx_, "SELECT %d", config_.db));
        RedisReply reply(r);

        if (!reply || reply.is_error())
            throw RedisConfigError(
                "Redis SELECT " + std::to_string(config_.db) + " failed: " +
                (reply ? std::string(reply->str) : "null reply"));
    }

    void RedisConnection::check_connection()
    {
        if (!is_connected())
            throw RedisCommandError("Redis conneciton is not open");
    }

    bool RedisConnection::ping()
    {
        check_connection();
        auto *r = static_cast<redisReply *>(redisCommand(ctx_, "PING"));
        RedisReply reply(r);
        return reply && reply.is_status() &&
               std::string(reply->str) == "PONG";
    }

    void RedisConnection::set(const std::string &key, const std::string &value, int ttl_seconds)
    {
        check_connection();
        redisReply *r;
        if (ttl_seconds > 0)
        {
            r = static_cast<redisReply *>(
                redisCommand(ctx_, "SET %s %b EX %d",
                             key.c_str(),
                             value.data(), value.size(),
                             ttl_seconds));
        }
        else
        {
            r = static_cast<redisReply *>(
                redisCommand(ctx_, "SET %s %b",
                             key.c_str(),
                             value.data(), value.size()));
        }
        RedisReply reply(r);
        if (!reply || reply.is_error())
            throw RedisCommandError(
                "SET failed: " +
                (reply ? std::string(reply->str) : "null reply"));
    }

    std::string RedisConnection::get(const std::string &key)
    {
        check_connection();
        auto *r = static_cast<redisReply *>(
            redisCommand(ctx_, "GET %s", key.c_str()));
        RedisReply reply(r);

        if (!reply)
            throw RedisCommandError("GET returned null reply");
        if (reply.is_error())
            throw RedisCommandError("GET failed: " + std::string(reply->str));
        if (reply.is_nil())
            return "";

        return reply.str();
    }

    long long RedisConnection::del(const std::string &key)
    {
        check_connection();
        auto *r = static_cast<redisReply *>(
            redisCommand(ctx_, "DEL %s", key.c_str()));
        RedisReply reply(r);

        if (!reply || reply.is_error())
            throw RedisCommandError(
                "DEL failed: " +
                (reply ? std::string(reply->str) : "null reply"));
        return reply.integer();
    }

    long long RedisConnection::exists(const std::string &key)
    {
        check_connection();
        auto *r = static_cast<redisReply *>(
            redisCommand(ctx_, "EXISTS %s", key.c_str()));
        RedisReply reply(r);

        if (!reply || reply.is_error())
            throw RedisCommandError(
                "EXISTS failed: " +
                (reply ? std::string(reply->str) : "null reply"));
        return reply.integer();
    }

    void RedisConnection::expire(const std::string &key, int seconds)
    {
        check_connection();
        auto *r = static_cast<redisReply *>(
            redisCommand(ctx_, "EXPIRE %s %d", key.c_str(), seconds));
        RedisReply reply(r);

        if (!reply || reply.is_error())
            throw RedisCommandError(
                "EXPIRE failed: " +
                (reply ? std::string(reply->str) : "null reply"));
    }

    void RedisConnection::hset(const std::string &key,
                               const std::string &field,
                               const std::string &value)
    {
        check_connection();
        auto *r = static_cast<redisReply *>(
            redisCommand(ctx_, "HSET %s %s %b",
                         key.c_str(), field.c_str(),
                         value.data(), value.size()));
        RedisReply reply(r);

        if (!reply || reply.is_error())
            throw RedisCommandError(
                "HSET failed: " +
                (reply ? std::string(reply->str) : "null reply"));
    }

    std::string RedisConnection::hget(const std::string &key, const std::string &field)
    {
        check_connection();
        auto *r = static_cast<redisReply *>(
            redisCommand(ctx_, "HGET %s %s", key.c_str(), field.c_str()));
        RedisReply reply(r);

        if (!reply)
            throw RedisCommandError("HGET returned null reply");
        if (reply.is_error())
            throw RedisCommandError("HGET failed: " + std::string(reply->str));
        if (reply.is_nil())
            return "";

        return reply.str();
    }

    std::unordered_map<std::string, std::string>
    RedisConnection::hgetall(const std::string &key)
    {
        check_connection();
        auto *r = static_cast<redisReply *>(
            redisCommand(ctx_, "HGETALL %s", key.c_str()));
        RedisReply reply(r);

        if (!reply)
            throw RedisCommandError("HGETALL returned null reply");
        if (reply.is_error())
            throw RedisCommandError("HGETALL failed: " + std::string(reply->str));
        if (reply.is_nil())
            return {};

        std::unordered_map<std::string, std::string> result;
        for (size_t i = 0; i < reply->elements; i += 2)
        {
            result[reply->element[i]->str] =
                reply->element[i + 1]->str;
        }
        return result;
    }

    void RedisConnection::hmset(
        const std::string &key,
        const std::unordered_map<std::string, std::string> &fields)
    {
        if (fields.empty())
            return;
        check_connection();

        std::vector<const char *> argv;
        std::vector<std::size_t> argvlen;

        argv.push_back("HMSET");
        argvlen.push_back(5);
        argv.push_back(key.c_str());
        argvlen.push_back(key.size());

        for (const auto &[f, v] : fields)
        {
            argv.push_back(f.c_str());
            argvlen.push_back(f.size());
            argv.push_back(v.c_str());
            argvlen.push_back(v.size());
        }

        auto *r = static_cast<redisReply *>(
            redisCommandArgv(ctx_,
                             static_cast<int>(argv.size()),
                             argv.data(),
                             argvlen.data()));
        RedisReply reply(r);

        if (!reply || reply.is_error())
            throw RedisCommandError(
                "HMSET failed: " +
                (reply ? std::string(reply->str) : "null reply"));
    }

    long long RedisConnection::hdel(const std::string &key,
                                    const std::string &field)
    {
        check_connection();
        auto *r = static_cast<redisReply *>(
            redisCommand(ctx_, "HDEL %s %s", key.c_str(), field.c_str()));
        RedisReply reply(r);

        if (!reply || reply.is_error())
            throw RedisCommandError(
                "HDEL failed: " +
                (reply ? std::string(reply->str) : "null reply"));
        return reply.integer();
    }

    RedisConfig redis_config_from_env()
    {
        auto get = [](const char *var, const char *fb) -> std::string
        {
            const char *val = std::getenv(var);
            return (val && *val) ? val : fb;
        };

        RedisConfig cfg;
        cfg.host = get("CORVUS_REDIS_HOST", "localhost");
        cfg.port = std::stoi(get("CORVUS_REDIS_PORT", "6379"));
        cfg.password = get("CORVUS_REDIS_PASSWORD", "");
        cfg.db = std::stoi(get("CORVUS_REDIS_DB", "0"));
        return cfg;
    }

} // namespace corvus::db