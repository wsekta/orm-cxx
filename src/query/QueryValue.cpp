module;

#include <array>
#include <chrono>
#include <compare>
#include <string>
#include <variant>

module orm;

import :query_value;

namespace orm::query
{
auto QueryValue::operator==(const QueryValue& other) const -> bool
{
    return logicalType == other.logicalType && value == other.value;
}

auto QueryValue::operator<=>(const QueryValue& other) const -> std::partial_ordering
{
    if (const auto order = logicalType <=> other.logicalType; order != 0)
        return order;
    return value <=> other.value;
}
}
