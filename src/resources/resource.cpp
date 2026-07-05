#include "corvus/resources/resource.h"

namespace corvus::resources
{

    namespace
    {

        void require_non_empty(const std::string &field, const std::string &value)
        {
            if (value.empty())
                throw ResourceValidationError(field + " must not be empty");
        }

        void require_max_length(const std::string &field,
                                const std::string &value,
                                std::size_t max)
        {
            if (value.size() > max)
                throw ResourceValidationError(
                    field + " exceeds maximum length of " +
                    std::to_string(max) + " characters");
        }

        void validate_kind(const std::string &kind)
        {
            require_non_empty("kind", kind);
            require_max_length("kind", kind, kKindMaxLen);
        }

        void validate_name(const std::string &name)
        {
            require_non_empty("name", name);
            require_max_length("name", name, kNameMaxLen);
        }

        void validate_status(const std::string &status)
        {
            require_non_empty("status", status);
            require_max_length("status", status, kStatusMaxLen);
        }

        void validate_metadata(const nlohmann::json &metadata)
        {
            if (!metadata.is_object())
                throw ResourceValidationError(
                    "metadata must be a JSON object");

            const auto serialized = metadata.dump();
            if (serialized.size() > kMetadataMaxBytes)
                throw ResourceValidationError(
                    "metadata exceeds maximum size of " +
                    std::to_string(kMetadataMaxBytes) + " bytes");
        }

        std::string require_string(const nlohmann::json &body,
                                   const std::string &field)
        {
            if (!body.contains(field))
                throw ResourceValidationError(
                    field + " is required");
            if (!body[field].is_string())
                throw ResourceValidationError(
                    field + " must be a string");
            return body[field].get<std::string>();
        }

        std::optional<std::string>
        optional_string(const nlohmann::json &body, const std::string &field)
        {
            if (!body.contains(field) || body[field].is_null())
                return std::nullopt;
            if (!body[field].is_string())
                throw ResourceValidationError(
                    field + " must be a string");
            return body[field].get<std::string>();
        }

        std::optional<nlohmann::json>
        optional_object(const nlohmann::json &body, const std::string &field)
        {
            if (!body.contains(field) || body[field].is_null())
                return std::nullopt;
            if (!body[field].is_object())
                throw ResourceValidationError(
                    field + " must be a JSON object");
            return body[field];
        }

    } // namespace

    nlohmann::json to_json(const Resource &r)
    {
        return nlohmann::json{
            {"id", r.id},
            {"client_id", r.client_id},
            {"kind", r.kind},
            {"name", r.name},
            {"status", r.status},
            {"metadata", r.metadata},
            {"created_at", r.created_at},
            {"updated_at", r.updated_at},
        };
    }

    CreateResourceRequest parse_create_request(const nlohmann::json &body)
    {
        if (!body.is_object())
            throw ResourceValidationError("request body must be a JSON object");

        CreateResourceRequest req;
        req.kind = require_string(body, "kind");
        req.name = require_string(body, "name");
        req.status = optional_string(body, "status");
        req.metadata = optional_object(body, "metadata");

        validate_kind(req.kind);
        validate_name(req.name);
        if (req.status)
            validate_status(*req.status);
        if (req.metadata)
            validate_metadata(*req.metadata);

        return req;
    }

    UpdateResourceRequest parse_update_request(const nlohmann::json &body)
    {
        if (!body.is_object())
            throw ResourceValidationError("request body must be a JSON object");

        UpdateResourceRequest req;
        req.name = optional_string(body, "name");
        req.status = optional_string(body, "status");
        req.metadata = optional_object(body, "metadata");

        if (!req.name && !req.status && !req.metadata)
            throw ResourceValidationError(
                "request body must contain at least one of: "
                "name, status, metadata");

        if (req.name)
            validate_name(*req.name);
        if (req.status)
            validate_status(*req.status);
        if (req.metadata)
            validate_metadata(*req.metadata);

        return req;
    }

} // namespace corvus::resources