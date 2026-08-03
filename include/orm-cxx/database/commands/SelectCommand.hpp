#pragma once

#include "orm-cxx/database/SelectStatement.hpp"
#include "orm-cxx/model/ModelView.hpp"
#include "orm-cxx/query/SelectSpec.hpp"

namespace orm::db::commands
{
class SelectCommand
{
public:
    virtual ~SelectCommand() = default;

    [[nodiscard]] virtual auto select(model::ModelView model,
                                      const query::SelectSpec& spec) const -> SelectStatement = 0;
};
} // namespace orm::db::commands
