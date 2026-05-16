#include "corvus/api/request_id.h"
#include <random>
#include <sstream>
#include <iomanip>

namespace corvus::api {

std::string generate_uuid()
{
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<uint32_t> dist{0, 0xFFFFFFFF};

    const uint32_t a = dist(rng);
    const uint32_t b = dist(rng);
    const uint32_t c = (dist(rng) & 0x0FFF) | 0x4000; // version 4
    const uint32_t d = (dist(rng) & 0x3FFF) | 0x8000; // variant bits
    const uint32_t e = dist(rng);
    const uint32_t f = dist(rng);

    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(8) << a << '-'
        << std::setw(4) << (b >> 16) << '-'
        << std::setw(4) << c << '-'
        << std::setw(4) << d << '-'
        << std::setw(4) << (e >> 16)
        << std::setw(8) << f;
    return oss.str();
}

std::string get_request_id(const drogon::HttpRequestPtr& req)
{
    const auto& attrs = req->getAttributes();
    if (auto val = attrs->get<std::string>(corvus::api::kRequestIdKey);
        !val.empty()) {
        return val;
    }
    const auto header = req->getHeader("X-Request-ID");
    if (!header.empty()) {
        return std::string{header};
    }
    return generate_uuid();
}

} // namespace corvus::api