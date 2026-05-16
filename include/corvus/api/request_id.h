#pragma once
#include <drogon/drogon.h>
#include <string>

namespace corvus::api
{

    /// Key used to store the request ID in drogon request attributes
    inline constexpr const char *kRequestIdKey = "corvus-request-id";

    /// Extract the request ID previously injected by RequestIdFilter
    std::string get_request_id(const drogon::HttpRequestPtr &req);

    /// Generate a new UUID v4 string
    std::string generate_uuid();

} // namespace corvus::api