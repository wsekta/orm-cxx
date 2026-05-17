#pragma once

#include <string>
#include <vector>

#include "orm-cxx/query/QueryValue.hpp"

namespace orm::db
{
struct SelectStatement
{
    std::string sql;
    std::vector<query::QueryParameter> parameters;
};
} // namespace orm::db
