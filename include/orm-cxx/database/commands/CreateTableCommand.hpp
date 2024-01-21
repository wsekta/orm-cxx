#pragma once

#include "orm-cxx/model/ModelInfo.hpp"

namespace orm::db::commands
{
class CreateTableCommand
{
public:
    virtual ~CreateTableCommand() = default;

    [[nodiscard]] virtual auto createTable(const model::ModelInfo& modelInfo) const -> std::string = 0;
};
} // namespace orm::db
