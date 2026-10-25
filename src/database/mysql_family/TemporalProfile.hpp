#pragma once

#include <charconv>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace orm_mysql_temporal
{
inline auto serverVersion(std::string_view version) -> std::pair<unsigned int, unsigned int>
{
    // MariaDB can retain the MySQL compatibility prefix in its version string.
    if (version.starts_with("5.5.5-"))
        version.remove_prefix(6);
    unsigned int major{};
    unsigned int minor{};
    const auto first = std::from_chars(version.data(), version.data() + version.size(), major);
    if (first.ec != std::errc{} || first.ptr == version.data() + version.size() || *first.ptr != '.')
        throw std::invalid_argument{"The server version cannot be parsed"};
    const auto second = std::from_chars(first.ptr + 1, version.data() + version.size(), minor);
    if (second.ec != std::errc{} || second.ptr == version.data() + version.size() || *second.ptr != '.')
        throw std::invalid_argument{"The server version cannot be parsed"};
    return {major, minor};
}

inline auto timestampMaximum(std::string_view version, bool maria) -> std::pair<long long, int>
{
    const auto [major, minor] = serverVersion(version);
    if (maria && (major < 10 || (major == 10 && minor < 7)))
        throw std::invalid_argument{"Native UUID requires MariaDB 10.7 or newer"};
    const bool extended = maria && (major > 11 || (major == 11 && minor >= 5));
    return {extended ? 4'294'967'295LL : 2'147'483'647LL, maria ? 999999 : 499999};
}
}
