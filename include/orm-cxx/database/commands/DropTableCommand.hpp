#pragma once

#include "orm-cxx/model/ModelView.hpp"

namespace orm::db::commands
{
class DropTableCommand
{
public:
    virtual ~DropTableCommand() = default;

    [[nodiscard]] virtual auto dropTable(model::ModelView model) const -> std::string = 0;
};
}
