#pragma once

#include "orm-cxx/database/commands/InsertCommand.hpp"

namespace orm::db::commands
{
class DefaultInsertCommand : public InsertCommand
{
public:
    [[nodiscard]] auto insert(const model::ModelInfo& modelInfo) const -> std::string override;
};
} // namespace orm::db::commands
