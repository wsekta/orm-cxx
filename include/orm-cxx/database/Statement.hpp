#pragma once

#include <optional>
#include <string>
#include <vector>

#include "orm-cxx/model/ColumnType.hpp"
#include "orm-cxx/query/QueryValue.hpp"

namespace orm::db
{
struct BoundValue
{
    model::ColumnType logicalType = model::ColumnType::Unknown;
    std::optional<query::QueryValue::Value> value;

    [[nodiscard]] auto isNull() const noexcept -> bool
    {
        return not value.has_value();
    }
};

struct StatementParameter
{
    std::string name;
    std::optional<query::QueryValue> value;
    model::ColumnType nullType = model::ColumnType::Unknown;

    [[nodiscard]] auto getLogicalType() const noexcept -> model::ColumnType
    {
        return value.has_value() ? value->getLogicalType() : nullType;
    }

    [[nodiscard]] auto getBoundValue() const -> BoundValue
    {
        if (value.has_value())
        {
            return BoundValue{.logicalType = value->getLogicalType(), .value = value->get()};
        }

        return BoundValue{.logicalType = nullType, .value = std::nullopt};
    }
};

struct Statement
{
    std::string sql;
    std::vector<StatementParameter> parameters;
};
} // namespace orm::db
