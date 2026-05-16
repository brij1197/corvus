#include "corvus/api/envelope.h"
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>

namespace corvus::api
{

    namespace
    {

        std::string utc_now_iso8601()
        {
            const auto now = std::chrono::system_clock::now();
            const auto time = std::chrono::system_clock::to_time_t(now);
            std::ostringstream oss;
            std::tm tm_buf{};
#ifdef _WIN32
            gmtime_s(&tm_buf, &time);
#else
            gmtime_r(&time, &tm_buf);
#endif
            oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%SZ");
            return oss.str();
        }

    } // namespace

    Envelope ok(Json::Value data, const std::string &request_id)
    {
        return Envelope{
            .data = std::move(data),
            .error = std::nullopt,
            .meta = Meta{
                .request_id = request_id,
                .timestamp = utc_now_iso8601(),
                .next_cursor = std::nullopt,
                .has_more = std::nullopt,
            },
        };
    }

    Envelope ok_list(Json::Value data,
                     const std::string &request_id,
                     std::optional<std::string> next_cursor,
                     bool has_more)
    {
        return Envelope{
            .data = std::move(data),
            .error = std::nullopt,
            .meta = Meta{
                .request_id = request_id,
                .timestamp = utc_now_iso8601(),
                .next_cursor = std::move(next_cursor),
                .has_more = has_more,
            },
        };
    }

    Envelope error(ErrorCode code,
                   const std::string &message,
                   const std::string &request_id,
                   Json::Value details)
    {
        return Envelope{
            .data = Json::Value{Json::nullValue},
            .error = ApiError{
                .code = code,
                .message = message,
                .details = std::move(details),
            },
            .meta = Meta{
                .request_id = request_id,
                .timestamp = utc_now_iso8601(),
                .next_cursor = std::nullopt,
                .has_more = std::nullopt,
            },
        };
    }

} // namespace corvus::api