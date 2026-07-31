#pragma once

#include "orm-cxx/model/ModelView.hpp"

namespace orm::db::commands
{
class CreateTableCommand
{
public:
    virtual ~CreateTableCommand() = default;

    [[nodiscard]] virtual auto createTable(model::ModelView model) const -> std::string = 0;
};
} // namespace orm::db
