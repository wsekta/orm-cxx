#pragma once

#include "orm-cxx/database/commands/UpdateCommand.hpp"
#include "SqlRenderer.hpp"

namespace orm::db::commands
{
class DefaultUpdateCommand : public UpdateCommand
{
public:
    explicit DefaultUpdateCommand(const SqlDialect& dialect);

    [[nodiscard]] auto update(model::ModelView model,
                              const query::detail::UpdateSpec& spec) const -> Statement override;

private:
    const SqlDialect& dialect;
};
} // namespace orm::db::commands
