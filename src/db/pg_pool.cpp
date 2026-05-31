#include "corvus/db/pg_pool.h"
#include <cstdlib>
#include <stdexcept>

namespace corvus::db
{

    PgConnection::PgConnection(std::unique_ptr<pqxx::connection> conn, ReturnFn return_fn)
        : conn_(std::move(conn)), return_fn_(std::move(return_fn))
    {
    }

    PgConnection::~PgConnection()
    {
        if (conn_ && return_fn_)
        {
            return_fn_(std::move(conn_));
        }
    }

    PgConnection::PgConnection(PgConnection &&other) noexcept
        : conn_(std::move(other.conn_)), return_fn_(std::move(other.return_fn_))
    {
    }

    PgConnection &PgConnection::operator=(PgConnection &&other) noexcept
    {
        if (this != &other)
        {
            if (conn_ && return_fn_)
            {
                return_fn_(std::move(conn_));
            }
            conn_ = std::move(other.conn_);
            return_fn_ = std::move(other.return_fn_);
        }
        return *this;
    }

    PgPool::PgPool(PoolConfig config)
        : config_(std::move(config))
    {
        if (config_.min_connections == 0)
        {
            throw DbConfigError("min_connections must be at least 1");
        }

        if (config_.max_connections < config_.min_connections)
        {
            throw DbConfigError("max_connections must be >= min_connections");
        }

        if (config_.connection_string.empty())
        {
            throw DbConfigError("connection_string must not be empty");
        }

        std::lock_guard<std::mutex> lock(mutex_);
        for (std::size_t i = 0; i < config_.min_connections; ++i)
        {
            idle_.push(make_connection());
            ++total_;
        }
    }

    PgPool::~PgPool()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!idle_.empty())
        {
            idle_.pop();
        }
    }

    PgConnection PgPool::acquire()
    {
        std::unique_lock<std::mutex> lock(mutex_);

        const auto deadline = std::chrono::steady_clock::now() + config_.acquire_timeout;

        while (true)
        {
            if (!idle_.empty())
            {
                auto conn = std::move(idle_.front());
                idle_.pop();

                if (!conn->is_open())
                {
                    try
                    {
                        conn = make_connection();
                    }
                    catch (...)
                    {
                        --total_;
                        continue;
                    }
                }
                return PgConnection(
                    std::move(conn),
                    [this](std::unique_ptr<pqxx::connection> rc)
                    {
                        return_connection(std::move(rc));
                    });
            }

            if (total_ < config_.max_connections)
            {
                auto conn = make_connection();
                ++total_;
                return PgConnection(
                    std::move(conn),
                    [this](std::unique_ptr<pqxx::connection> rc)
                    {
                        return_connection(std::move(rc));
                    });
            }

            if (cv_.wait_until(lock, deadline) == std::cv_status::timeout)
            {
                throw DbPoolExhausted("No PostgreSQL connection available after " + std::to_string(config_.acquire_timeout.count()) + "ms");
            }
        }
    }

    std::size_t PgPool::available() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return idle_.size();
    }

    std::size_t PgPool::size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return total_;
    }

    std::unique_ptr<pqxx::connection> PgPool::make_connection()
    {
        try
        {
            return std::make_unique<pqxx::connection>(config_.connection_string);
        }
        catch (const std::exception &e)
        {
            throw DbConfigError(std::string("Failed to connect to PostgreSQL: ") + e.what());
        }
    }

    void PgPool::return_connection(std::unique_ptr<pqxx::connection> conn)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (conn && conn->is_open())
            {
                idle_.push(std::move(conn));
            }
            else
            {
                --total_;
            }
        }
        cv_.notify_one();
    }

    std::string connection_string_from_env()
    {
        auto get = [](const char *var, const char *fallback) -> std::string
        {
            const char *val = std::getenv(var);
            return (val && *val) ? val : fallback;
        };

        const auto host = get("CORVUS_DB_HOST", "localhost");
        const auto port = get("CORVUS_DB_PORT", "5432");
        const auto dbname = get("CORVUS_DB_NAME", "corvus");
        const auto user = get("CORVUS_DB_USER", "postgres");
        const auto pass = get("CORVUS_DB_PASSWORD", "");

        return "host=" + host +
               " port=" + port +
               " dbname=" + dbname +
               " user=" + user +
               " password=" + pass +
               " connect_timeout=5";
    }
} // namespace corvus::db