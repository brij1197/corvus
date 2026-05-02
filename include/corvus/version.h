#pragma once
#include <string_view>

namespace corvus::version {

constexpr int major_v = 0;
constexpr int minor_v = 1;
constexpr int patch_v = 0;

constexpr std::string_view string() noexcept
{
    return "0.1.0";
}

} // namespace corvus::version
