#pragma once
#include <drogon/drogon.h>
#include <string>

namespace corvus::auth
{
    inline constexpr const char *kClientIdKey = "corvus-client-id";

    std::string get_client_id(const drogon::HttpRequestPtr &req);

} // namespace corvus::auth