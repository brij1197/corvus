#pragma once
#include "corvus/db/pg_pool.h"
#include "corvus/db/redis_connection.h"
#include "corvus/gateway/health_handler.h"
#include <memory>

namespace corvus::gateway
{

    HandlerFunc readiness_handler(std::shared_ptr<db::PgPool> pool,
                                  db::RedisConfig redis_config);

} // namespace corvus::gateway