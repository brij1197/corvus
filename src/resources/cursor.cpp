#include "corvus/resources/cursor.h"
#include "corvus/resources/resource_repository.h"
#include <array>
#include <regex>

namespace corvus::resources
{
    namespace
    {
        constexpr char kAlphabet[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

        std::array<int, 256> decode_table()
        {
            std::array<int, 256> t{};
            t.fill(-1);
            for (int i = 0; i < 64; ++i)
                t[static_cast<unsigned char>(kAlphabet[i])] = i;
            t[static_cast<unsigned char>('+')] = 62;
            t[static_cast<unsigned char>('/')] = 63;
            return t;
        }

        const std::regex kTimestamp(R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{6}Z)");
        const std::regex kUuid(
            R"([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})");
    } // namespace

    std::string base64url_encode(const std::string &in)
    {
        std::string out;
        out.reserve((in.size() * 4 + 2) / 3);
        unsigned int val = 0;
        int bits = 0;
        for (const char ch : in)
        {
            val = (val << 8) | static_cast<unsigned char>(ch);
            bits += 8;
            while (bits >= 6)
            {
                bits -= 6;
                out.push_back(kAlphabet[(val >> bits) & 0x3F]);
            }
        }
        if (bits > 0)
            out.push_back(kAlphabet[(val << (6 - bits)) & 0x3F]);
        return out;
    }

    std::string base64url_decode(const std::string &in)
    {
        static const auto table = decode_table();

        std::string body = in;
        while (!body.empty() && body.back() == '=')
            body.pop_back();
        if (body.size() % 4 == 1)
            throw InvalidCursorError("cursor has an impossible length");

        std::string out;
        unsigned int val = 0;
        int bits = 0;
        for (const char ch : body)
        {
            const int v = table[static_cast<unsigned char>(ch)];
            if (v < 0)
                throw InvalidCursorError("cursor contains a character outside base64url");
            val = (val << 6) | static_cast<unsigned int>(v);
            bits += 6;
            if (bits >= 8)
            {
                bits -= 8;
                out.push_back(static_cast<char>((val >> bits) & 0xFF));
            }
        }
        return out;
    }

    std::string encode_cursor(const CursorPosition &pos)
    {
        return base64url_encode(pos.created_at + "|" + pos.id);
    }

    CursorPosition decode_cursor(const std::string &cursor)
    {
        if (cursor.empty() || cursor.size() > 128)
            throw InvalidCursorError("cursor is empty or too long");

        const auto decoded = base64url_decode(cursor);
        const auto sep = decoded.find('|');
        if (sep == std::string::npos)
            throw InvalidCursorError("cursor is malformed");

        CursorPosition pos{decoded.substr(0, sep), decoded.substr(sep + 1)};
        if (!std::regex_match(pos.created_at, kTimestamp) || !std::regex_match(pos.id, kUuid))
            throw InvalidCursorError("cursor is malformed");
        return pos;
    }

} // namespace corvus::resources