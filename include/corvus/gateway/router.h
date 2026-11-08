#pragma once
#include "corvus/db/pg_pool.h"
#include "corvus/db/redis_connection.h"
#include "corvus/gateway/blocking_executor.h"
#include <memory>

namespace corvus::gateway
{

    void register_routes();
    void register_readiness_route(std::shared_ptr<db::PgPool> pool,
                                  db::RedisConfig redis_config,
                                  std::shared_ptr<BlockingExecutor> executor);
    void register_catchall_routes();

} // namespace corvus::gateway
