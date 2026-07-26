#include "corvus/auth/client_context.h"

namespace corvus::auth
{
    std::string get_client_id(const drogon::HttpRequestPtr &req)
    {
        const auto &attrs = req->getAttributes();
        return attrs->get<std::string>(kClientIdKey);
    }

} // namespace corvus:auth