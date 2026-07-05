#pragma once
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>

namespace corvus::resources
{

    struct ResourceValidationError : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    constexpr std::size_t kNameMaxLen = 256;
    constexpr std::size_t kKindMaxLen = 64;
    constexpr std::size_t kStatusMaxLen = 32;
    constexpr std::size_t kMetadataMaxBytes = 16 * 1024;

    struct Resource
    {
        std::string id;
        std::string client_id;
        std::string kind;
        std::string name;
        std::string status;
        nlohmann::json metadata;
        std::string created_at;
        std::string updated_at;
    };

    nlohmann::json to_json(const Resource &r);

    struct CreateResourceRequest
    {
        std::string kind;
        std::string name;
        std::optional<std::string> status;
        std::optional<nlohmann::json> metadata;
    };

    CreateResourceRequest parse_create_request(const nlohmann::json &body);

    struct UpdateResourceRequest
    {
        std::optional<std::string> name;
        std::optional<std::string> status;
        std::optional<nlohmann::json> metadata;
    };

    UpdateResourceRequest parse_update_request(const nlohmann::json &body);

} // namespace corvus::resources