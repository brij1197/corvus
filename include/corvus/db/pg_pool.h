#pragma once
#include <pqxx/pqxx>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>

namespace corvus::db
{

    struct DbConfigError : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    struct DbPoolExhausted : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    struct PoolConfig
    {
        std::string connection_string;
        std::size_t min_connections{2};
        std::size_t max_connections{10};
        std::chrono::milliseconds acquire_timeout{5000};
    };

    class PgConnection
    {
    public:
        using ReturnFn = std::function<void(std::unique_ptr<pqxx::connection>)>;
        pqxx::connection &get() { return *conn_; }
        pqxx::connection *operator->() { return conn_.get(); }
        pqxx::connection &operator*() { return *conn_; }

        ~PgConnection();
        PgConnection(const PgConnection &) = delete;
        PgConnection &operator=(const PgConnection &) = delete;
        PgConnection(PgConnection &&) noexcept;
        PgConnection &operator=(PgConnection &&) noexcept;

    private:
        friend class PgPool;
        PgConnection(std::unique_ptr<pqxx::connection> conn, ReturnFn return_fn);

        std::unique_ptr<pqxx::connection> conn_;
        ReturnFn return_fn_;
    };

    class PgPool
    {
    public:
        explicit PgPool(PoolConfig config);
        ~PgPool();

        PgConnection acquire();

        std::size_t available() const;

        std::size_t size() const;

        const PoolConfig &config() const noexcept { return config_; }

        PgPool(const PgPool &) = delete;
        PgPool &operator=(const PgPool &) = delete;

    private:
        std::unique_ptr<pqxx::connection> make_connection();
        void return_connection(std::unique_ptr<pqxx::connection> conn);

        PoolConfig config_;

        mutable std::mutex mutex_;
        std::condition_variable cv_;
        std::queue<std::unique_ptr<pqxx::connection>> idle_;
        std::size_t total_{0};
    };

    std::string connection_string_from_env();

} // namespace corvus::db