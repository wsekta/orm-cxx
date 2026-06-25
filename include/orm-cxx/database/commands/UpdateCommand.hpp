#pragma once

#include "orm-cxx/database/Statement.hpp"
#include "orm-cxx/query/UpdateData.hpp"

namespace orm::db::commands
{
class UpdateCommand
{
public:
    virtual ~UpdateCommand() = default;

    [[nodiscard]] virtual auto update(const query::UpdateData& updateData) const -> Statement = 0;
};
} // namespace orm::db::commands
