#pragma once

#include <stdexcept>
#include <string_view>
#include <variant>

#include "ConversionError.hpp"
#include "NullBinding.hpp"
#include "orm-cxx/database/Statement.hpp"
#include "soci/values.h"

namespace orm::db::binding
{
inline auto bindNull(soci::values& values, std::string_view name, model::ColumnType logicalType) -> void
{
    switch (logicalType)
    {
    case model::ColumnType::Bool:
    case model::ColumnType::Char:
    case model::ColumnType::UnsignedChar:
    case model::ColumnType::Short:
    case model::ColumnType::UnsignedShort:
    case model::ColumnType::Int:
        bindTypedNull(values, name, int{});
        return;
    case model::ColumnType::UnsignedInt:
    case model::ColumnType::UnsignedLongLong:
        bindTypedNull(values, name, static_cast<unsigned long long>(0));
        return;
    case model::ColumnType::LongLong:
        bindTypedNull(values, name, static_cast<long long>(0));
        return;
    case model::ColumnType::Float:
    case model::ColumnType::Double:
        bindTypedNull(values, name, 0.0);
        return;
    case model::ColumnType::String:
        bindTypedNull(values, name, std::string{});
        return;
    case model::ColumnType::Uuid:
    case model::ColumnType::Unknown:
    case model::ColumnType::OneToOne:
        break;
    }

    throw std::invalid_argument{"Cannot bind NULL parameter with unsupported column type"};
}

inline auto hasCompatibleStorage(const BoundValue& value) -> bool
{
    if (value.isNull())
    {
        return true;
    }

    return query::QueryValue::isCompatibleStorage(value.logicalType, value.value.value());
}

inline auto bindBoundValue(soci::values& values, std::string_view name, const BoundValue& value) -> void
{
    if (value.isNull())
    {
        bindNull(values, name, value.logicalType);
        return;
    }

    if (not hasCompatibleStorage(value))
    {
        throw ConversionError{"Bound value storage does not match its logical column type"};
    }

    const auto parameterName = std::string{name};
    std::visit([&values, &parameterName](const auto& storedValue) { values.set(parameterName, storedValue); },
               value.value.value());
}

inline auto bindStatementParameter(soci::values& values, const StatementParameter& parameter) -> void
{
    bindBoundValue(values, parameter.name, parameter.getBoundValue());
}
} // namespace orm::db::binding
