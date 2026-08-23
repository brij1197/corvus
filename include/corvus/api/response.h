#pragma once
#include "corvus/api/envelope.h"
#include <drogon/drogon.h>

namespace corvus::api
{

  /// HTTP status codes mapped to each ErrorCode.
  inline drogon::HttpStatusCode http_status(ErrorCode code)
  {
    switch (code)
    {
    case ErrorCode::bad_request:
      return drogon::k400BadRequest;
    case ErrorCode::unauthorized:
      return drogon::k401Unauthorized;
    case ErrorCode::forbidden:
    case ErrorCode::policy_denied:
      return drogon::k403Forbidden;
    case ErrorCode::not_found:
    case ErrorCode::resource_not_found:
      return drogon::k404NotFound;
    case ErrorCode::conflict:
    case ErrorCode::resource_already_exists:
      return drogon::k409Conflict;
    case ErrorCode::too_many_requests:
      return drogon::k429TooManyRequests;
    case ErrorCode::unprocessable_entity:
    case ErrorCode::quota_exceeded:
      return drogon::k422UnprocessableEntity;
    case ErrorCode::payload_too_large:
      return drogon::k413RequestEntityTooLarge;
    case ErrorCode::internal_error:
    default:
      return drogon::k500InternalServerError;
    }
  }

  /// Wrap an Envelope into a Drogon HTTP response with correct status code.
  drogon::HttpResponsePtr to_response(const Envelope &env,
                                      drogon::HttpStatusCode status);

  drogon::HttpResponsePtr respond_ok(Json::Value data,
                                     const std::string &request_id);

  drogon::HttpResponsePtr respond_created(Json::Value data,
                                          const std::string &request_id);

  drogon::HttpResponsePtr respond_list(Json::Value data,
                                       const std::string &request_id,
                                       std::optional<std::string> next_cursor,
                                       bool has_more);

  drogon::HttpResponsePtr respond_error(ErrorCode code,
                                        const std::string &message,
                                        const std::string &request_id,
                                        Json::Value details = {});

} // namespace corvus::api