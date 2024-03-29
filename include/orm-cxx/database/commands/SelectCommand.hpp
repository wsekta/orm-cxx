#pragma once

#include "orm-cxx/query/QueryData.hpp"

namespace orm::db::commands
{
class SelectCommand
{
public:
    virtual ~SelectCommand() = default;

    [[nodiscard]] virtual auto select(const query::QueryData& queryData) const -> std::string = 0;
};
}
