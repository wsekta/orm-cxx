#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

namespace orm_postgresql_connection_detail
{
// SOCI 4.1.4 quotes spaces/apostrophes, but does not protect backslashes or
// other whitespace. Prepare only these bytes before its own serializer.
inline auto prepareValue(std::string_view value) -> std::string
{
    std::string result;
    result.reserve(value.size());
    for (const auto byte : value)
    {
        if (byte == '\0')
            throw std::invalid_argument{"Invalid PostgreSQL connection configuration"};
        if (byte == '\\' || byte == '\t' || byte == '\r' || byte == '\n' || byte == '\v' || byte == '\f')
            result.push_back('\\');
        result.push_back(byte);
    }
    return result;
}
}
