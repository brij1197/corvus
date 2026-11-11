#pragma once
#include <stdexcept>
#include <string>

namespace corvus::resources
{
    struct CursorPosition
    {
        std::string created_at;
        std::string id;

        bool operator==(const CursorPosition &other) const
        {
            return created_at == other.created_at && id == other.id;
        }
    };

    std::string encode_cursor(const CursorPosition &pos);

    CursorPosition decode_cursor(const std::string &cursor);

    std::string base64url_encode(const std::string &in);
    std::string base64url_decode(const std::string &in);

} // namespace corvus::resources