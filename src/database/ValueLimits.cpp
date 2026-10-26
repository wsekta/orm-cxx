module;

#include <chrono>
#include <compare>
#include <optional>
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
    const auto within = [](const auto& value, const auto& minimum, const auto& maximum) noexcept
    { return (!minimum || value >= *minimum) && (!maximum || value <= *maximum); };
    if (const auto* value = std::get_if<Date>(&*bound.value))
        return within(*value, limits.minDate, limits.maxDate);
    if (const auto* value = std::get_if<LocalDateTime>(&*bound.value))
        return within(*value, limits.minLocalDateTime, limits.maxLocalDateTime);
    if (const auto* value = std::get_if<UtcTimestamp>(&*bound.value))
        return within(*value, limits.minUtcTimestamp, limits.maxUtcTimestamp) &&
               within(*value, runtime.minUtcTimestamp, runtime.maxUtcTimestamp);
    if (const auto* value = std::get_if<unsigned long long>(&*bound.value))
        return !limits.maxUnsignedLongLong || *value <= *limits.maxUnsignedLongLong;
    return true;
}
}
