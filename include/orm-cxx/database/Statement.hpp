#pragma once

#include <optional>
#include <string>
#include <vector>

#include "orm-cxx/model/ColumnType.hpp"
#include "orm-cxx/query/QueryValue.hpp"

namespace orm::db
{
struct StatementParameter
{
    std::string name;
    std::optional<query::QueryValue> value;
    model::ColumnType nullType = model::ColumnType::Unknown;
};

struct Statement
{
    std::string sql;
    std::vector<StatementParameter> parameters;
};
} // namespace orm::db
