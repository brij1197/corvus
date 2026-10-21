#pragma once
#include "corvus/auth/api_key_validator.h"
#include "corvus/db/pg_pool.h"
#include <memory>

namespace corvus::auth
{
    ApiKeyLookup make_pg_api_key_lookup(std::shared_ptr<db::PgPool> pool);
} // namespace corvus::auth