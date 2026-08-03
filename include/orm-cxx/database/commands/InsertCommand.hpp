#pragma once

#include "orm-cxx/model/ModelView.hpp"

namespace orm::db::commands
{
class InsertCommand
{
public:
    virtual ~InsertCommand() = default;

    [[nodiscard]] virtual auto insert(model::ModelView model) const -> std::string = 0;
};
}
