#pragma once

#include "orm-cxx/database/commands/DeleteCommand.hpp"
#include "SqlRenderer.hpp"

namespace orm::db::commands
{
class DefaultDeleteCommand : public DeleteCommand
{
public:
    [[nodiscard]] auto remove(const model::ModelInfo& modelInfo, const query::Predicate& predicate) const
        -> Statement override;
};
} // namespace orm::db::commands
