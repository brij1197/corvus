#include "corvus/gateway/readiness_handler.h"
#include "corvus/api/response.h"
#include "corvus/api/request_id.h"
#include <drogon/drogon.h>

namespace corvus::gateway
{

    HandlerFunc readiness_handler(std::shared_ptr<db::PgPool> pool,
                                  db::RedisConfig redis_config)
    {
        return [pool, redis_config](
                   const drogon::HttpRequestPtr &req,
                   std::function<void(const drogon::HttpResponsePtr &)> &&cb)
        {
            const auto request_id = corvus::api::get_request_id(req);

            Json::Value checks(Json::objectValue);
            bool ready = true;

            try
            {
                auto conn = pool->acquire();
                pqxx::work txn(conn.get());
                txn.exec("SELECT 1");
                txn.commit();
                checks["postgres"] = "ok";
            }
            catch (const std::exception &e)
            {
                LOG_WARN << "readiness: postgres check failed: " << e.what();
                checks["postgres"] = "unavailable";
                ready = false;
            }

            try
            {
                db::RedisConnection redis(redis_config);
                if (redis.ping())
                {
                    checks["redis"] = "ok";
                }
                else
                {
                    checks["redis"] = "unavailable";
                    ready = false;
                }
            }
            catch (const std::exception &e)
            {
                LOG_WARN << "readiness: redis check failed: " << e.what();
                checks["redis"] = "unavailable";
                ready = false;
            }

            if (!ready)
            {
                auto resp = corvus::api::respond_error(
                    corvus::api::ErrorCode::service_unavailable,
                    "One or more dependencies are unavailable.",
                    request_id);
                resp->addHeader("X-Request-ID", request_id);
                cb(resp);
                return;
            }

            Json::Value data(Json::objectValue);
            data["status"] = "ready";
            data["checks"] = checks;

            auto resp = corvus::api::respond_ok(data, request_id);
            resp->addHeader("X-Request-ID", request_id);
            cb(resp);
        };
    }

} // namespace corvus::gateway