#pragma once

#include "orm-cxx/database/Statement.hpp"
#include "orm-cxx/model/ModelView.hpp"
#include "orm-cxx/query/UpdateSpec.hpp"

namespace orm::db::commands
{
class UpdateCommand
{
public:
    virtual ~UpdateCommand() = default;

    [[nodiscard]] virtual auto update(model::ModelView model, const query::UpdateSpec& spec) const -> Statement = 0;
};
} // namespace orm::db::commands
