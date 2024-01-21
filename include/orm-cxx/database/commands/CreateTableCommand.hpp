#pragma once

#include "orm-cxx/query/QueryData.hpp"

namespace orm::db::commands
{
class CreateTableCommand
{
public:
    virtual ~CreateTableCommand() = default;

    [[nodiscard]] virtual auto createTable(const model::ModelInfo& modelInfo) const -> std::string = 0;
};
} // namespace orm::db
