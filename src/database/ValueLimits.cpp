module;

#include <chrono>
#include <compare>
#include <optional>
#include <type_traits>
#include <variant>

module orm;

import :sql_contracts;

namespace orm::db::binding
{
auto isWithinValueLimits(const BoundValue& bound, const BackendValueLimits& limits,
                         const BackendRuntimeLimits& runtime) noexcept -> bool
{
    if (bound.isNull())
        return true;
    if (!query::QueryValue::isCompatibleStorage(bound.logicalType, *bound.value))
        return false;
    const auto within = [](const auto& value, const auto& minimum, const auto& maximum)
    { return (!minimum || value >= *minimum) && (!maximum || value <= *maximum); };
    return std::visit(
        [&](const auto& value)
        {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Date>)
                return within(value, limits.minDate, limits.maxDate);
            else if constexpr (std::is_same_v<T, LocalDateTime>)
                return within(value, limits.minLocalDateTime, limits.maxLocalDateTime);
            else if constexpr (std::is_same_v<T, UtcTimestamp>)
                return within(value, limits.minUtcTimestamp, limits.maxUtcTimestamp) &&
                       within(value, runtime.minUtcTimestamp, runtime.maxUtcTimestamp);
            else if constexpr (std::is_same_v<T, unsigned long long>)
                return !limits.maxUnsignedLongLong || value <= *limits.maxUnsignedLongLong;
            else
                return true;
        },
        *bound.value);
}
}
