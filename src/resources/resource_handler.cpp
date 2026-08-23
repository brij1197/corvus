#include "corvus/resources/resource_handler.h"
#include "corvus/auth/client_context.h"
#include "corvus/api/response.h"
#include "corvus/api/request_id.h"
#include <nlohmann/json.hpp>
#include <sstream>

namespace corvus::resources
{
    namespace
    {
        Json::Value to_json_value(const nlohmann::json &j)
        {
            Json::Value out;
            Json::CharReaderBuilder builder;
            std::string errors;
            std::istringstream stream(j.dump());

            if (!Json::parseFromStream(builder, stream, &out, &errors))
                throw std::runtime_error("Failed to convert resource JSON: " + errors);
            return out;
        }

    } // namespace

    ResourceHandler create_resource_handler(std::shared_ptr<ResourceService> service)
    {
        return [service](const drogon::HttpRequestPtr &req, HandlerCallback &&callback)
        {
            const auto request_id = corvus::api::get_request_id(req);
            const auto client_id = corvus::auth::get_client_id(req);

            if (client_id.empty())
            {
                LOG_ERROR << "client_id missing on authenticated route "
                          << "(request_id =" << request_id << ")";
                callback(corvus::api::respond_error(
                    corvus::api::ErrorCode::internal_error,
                    "An unexpected error occurred.",
                    request_id));
                return;
            }

            nlohmann::json body;
            try
            {
                body = nlohmann::json::parse(req->getBody());
            }
            catch (const nlohmann::json::exception &e)
            {
                LOG_DEBUG << "Malformed JSON body (request_id=" << request_id
                          << "): " << e.what();
                callback(corvus::api::respond_error(
                    corvus::api::ErrorCode::bad_request,
                    "Request body is not valid JSON.",
                    request_id));
                return;
            }

            try
            {
                const auto create_request = parse_create_request(body);
                const auto created = service->create(client_id, create_request);

                auto response = corvus::api::respond_created(
                    to_json_value(to_json(created)), request_id);
                response->addHeader("Location", "/v1/resources/" + created.id);
                callback(response);
            }
            catch (const ResourceValidationError &e)
            {
                callback(corvus::api::respond_error(
                    corvus::api::ErrorCode::unprocessable_entity,
                    e.what(),
                    request_id));
            }
            catch (const ResourceAlreadyExists &e)
            {
                callback(corvus::api::respond_error(
                    corvus::api::ErrorCode::resource_already_exists,
                    e.what(),
                    request_id));
            }
            catch (const std::exception &e)
            {
                LOG_ERROR << "create resource failed (request_id="
                          << request_id << "): " << e.what();
                callback(corvus::api::respond_error(
                    corvus::api::ErrorCode::internal_error,
                    "An unexpected error occured.",
                    request_id));
            }
        };
    }

} // namespace corvus::resources