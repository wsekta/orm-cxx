#pragma once

#include "orm-cxx/database/commands/UpdateCommand.hpp"
#include "SqlRenderer.hpp"

namespace orm::db::commands
{
class DefaultUpdateCommand : public UpdateCommand
{
public:
    [[nodiscard]] auto update(const query::UpdateData& updateData) const -> Statement override;

private:
    static auto getAssignments(const query::UpdateData& updateData, RenderContext& context) -> std::string;
};
} // namespace orm::db::commands
