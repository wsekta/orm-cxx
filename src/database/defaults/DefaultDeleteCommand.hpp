#pragma once

#include "orm-cxx/database/commands/DeleteCommand.hpp"
#include "SqlRenderer.hpp"

namespace orm::db::commands
{
class DefaultDeleteCommand : public DeleteCommand
{
public:
    explicit DefaultDeleteCommand(const SqlDialect& dialect);

    [[nodiscard]] auto remove(model::ModelView model, const query::Predicate& predicate) const -> Statement override;

private:
    const SqlDialect& dialect;
};
} // namespace orm::db::commands
