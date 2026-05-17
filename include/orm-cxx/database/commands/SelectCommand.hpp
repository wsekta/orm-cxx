#pragma once

#include "orm-cxx/database/SelectStatement.hpp"
#include "orm-cxx/query/QueryData.hpp"

namespace orm::db::commands
{
class SelectCommand
{
public:
    virtual ~SelectCommand() = default;

    [[nodiscard]] virtual auto select(const query::QueryData& queryData) const -> SelectStatement = 0;
};
} // namespace orm::db::commands
