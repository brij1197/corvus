#include "corvus/api/response.h"

namespace corvus::api
{
    drogon::HttpResponsePtr to_response(const Envelope &env,
                                        drogon::HttpStatusCode status)
    {
        auto resp = drogon::HttpResponse::newHttpJsonResponse(env.to_json());
        resp->setStatusCode(status);
        resp->addHeader("Content-Type", "application/json; charset=utf-8");
        return resp;
    }

    drogon::HttpResponsePtr respond_ok(Json::Value data,
                                       const std::string &request_id)
    {
        return to_response(ok(std::move(data), request_id), drogon::k200OK);
    }

    drogon::HttpResponsePtr respond_created(Json::Value data,
                                            const std::string &request_id)
    {
        return to_response(ok(std::move(data), request_id), drogon::k201Created);
    }

    drogon::HttpResponsePtr respond_list(Json::Value data,
                                         const std::string &request_id,
                                         std::optional<std::string> next_cursor,
                                         bool has_more)
    {
        return to_response(
            ok_list(std::move(data), request_id,
                    std::move(next_cursor), has_more),
            drogon::k200OK);
    }

    drogon::HttpResponsePtr respond_error(ErrorCode code,
                                          const std::string &message,
                                          const std::string &request_id,
                                          Json::Value details)
    {
        return to_response(
            error(code, message, request_id, std::move(details)),
            http_status(code));
    }

} // namespace corvus::api