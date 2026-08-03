#pragma once

#include "orm-cxx/database/commands/UpdateCommand.hpp"
#include "SqlRenderer.hpp"

namespace orm::db::commands
{
class DefaultUpdateCommand : public UpdateCommand
{
public:
    explicit DefaultUpdateCommand(const SqlDialect& dialect);

    [[nodiscard]] auto update(model::ModelView model, const query::UpdateSpec& spec) const -> Statement override;

private:
    const SqlDialect& dialect;

    static auto getAssignments(model::ModelView model, const query::UpdateSpec& spec,
                               RenderContext& context) -> std::string;
};
} // namespace orm::db::commands
