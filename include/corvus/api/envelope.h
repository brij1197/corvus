#pragma once
#include <chrono>
#include <json/json.h>
#include <optional>
#include <string>

namespace corvus::api
{

  /// Machine-readable error codes returned in the error envelope.
  /// Add new codes here as the API grows — never use raw strings in handlers.
  enum class ErrorCode
  {
    internal_error,
    not_found,
    bad_request,
    unauthorized,
    forbidden,
    conflict,
    too_many_requests,
    unprocessable_entity,
    resource_not_found,
    resource_already_exists,
    resource_invalid_state,
    quota_exceeded,
    policy_denied,
    payload_too_large,
  };

  /// Convert an ErrorCode to its canonical string representation.
  inline std::string to_string(ErrorCode code)
  {
    switch (code)
    {
    case ErrorCode::internal_error:
      return "INTERNAL_ERROR";
    case ErrorCode::not_found:
      return "NOT_FOUND";
    case ErrorCode::bad_request:
      return "BAD_REQUEST";
    case ErrorCode::unauthorized:
      return "UNAUTHORIZED";
    case ErrorCode::forbidden:
      return "FORBIDDEN";
    case ErrorCode::conflict:
      return "CONFLICT";
    case ErrorCode::too_many_requests:
      return "TOO_MANY_REQUESTS";
    case ErrorCode::unprocessable_entity:
      return "UNPROCESSABLE_ENTITY";
    case ErrorCode::resource_not_found:
      return "RESOURCE_NOT_FOUND";
    case ErrorCode::resource_already_exists:
      return "RESOURCE_ALREADY_EXISTS";
    case ErrorCode::resource_invalid_state:
      return "RESOURCE_INVALID_STATE";
    case ErrorCode::quota_exceeded:
      return "QUOTA_EXCEEDED";
    case ErrorCode::policy_denied:
      return "POLICY_DENIED";
    case ErrorCode::payload_too_large:
      return "PAYLOAD_TOO_LARGE";
    default:
      return "UNKNOWN_ERROR";
    }
  }

  struct ApiError
  {
    ErrorCode code;
    std::string message;
    Json::Value details{Json::objectValue};

    Json::Value to_json() const
    {
      Json::Value obj{Json::objectValue};
      obj["code"] = to_string(code);
      obj["message"] = message;
      if (!details.empty())
      {
        obj["details"] = details;
      }
      return obj;
    }
  };

  /// Metadata attached to every response.
  struct Meta
  {
    std::string request_id;
    std::string timestamp; // ISO 8601
    std::optional<std::string> next_cursor;
    std::optional<bool> has_more;

    Json::Value to_json() const
    {
      Json::Value obj{Json::objectValue};
      obj["request_id"] = request_id;
      obj["timestamp"] = timestamp;

      if (next_cursor.has_value())
      {
        obj["next_cursor"] = next_cursor.value();
      }
      if (has_more.has_value())
      {
        obj["has_more"] = has_more.value();
      }
      return obj;
    }
  };

  //  Envelope

  /// The standard JSON envelope wrapping every Corvus API response.
  /// Success: { "data": {...}, "error": null, "meta": {...} }
  /// Error:   { "data": null,  "error": {...}, "meta": {...} }

  struct Envelope
  {
    Json::Value data{Json::nullValue};
    std::optional<ApiError> error;
    Meta meta;

    Json::Value to_json() const
    {
      Json::Value obj{Json::objectValue};
      obj["data"] = data;
      obj["error"] =
          error.has_value() ? error->to_json() : Json::Value{Json::nullValue};
      obj["meta"] = meta.to_json();
      return obj;
    }
  };

  /// Build a success envelope with a data payload.
  Envelope ok(Json::Value data, const std::string &request_id);

  /// Build a success envelope for paginated list responses.
  Envelope ok_list(Json::Value data, const std::string &request_id,
                   std::optional<std::string> next_cursor = std::nullopt,
                   bool has_more = false);

  /// Build an error envelope.
  Envelope error(ErrorCode code, const std::string &message,
                 const std::string &request_id,
                 Json::Value details = Json::Value{Json::objectValue});

} // namespace corvus::api