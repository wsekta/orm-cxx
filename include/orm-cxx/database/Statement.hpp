#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "orm-cxx/model/ColumnType.hpp"
#include "orm-cxx/query/QueryValue.hpp"

namespace orm::db
{
struct BoundValue
{
    model::ColumnType logicalType;
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
    std::optional<model::ColumnType> nullType;

    [[nodiscard]] auto getLogicalType() const -> model::ColumnType
    {
        if (value.has_value())
        {
            return value->getLogicalType();
        }
        if (nullType.has_value())
        {
            return nullType.value();
        }
        throw std::invalid_argument{"A NULL statement parameter requires an explicit logical type"};
    }

    [[nodiscard]] auto getBoundValue() const -> BoundValue
    {
        if (value.has_value())
        {
            return BoundValue{.logicalType = value->getLogicalType(), .value = value->get()};
        }

        return BoundValue{.logicalType = getLogicalType(), .value = std::nullopt};
    }
};

struct Statement
{
    std::string sql;
    std::vector<StatementParameter> parameters;
};
} // namespace orm::db
