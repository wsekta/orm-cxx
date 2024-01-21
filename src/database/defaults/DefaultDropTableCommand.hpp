#pragma once

#include "orm-cxx/database/commands/DropTableCommand.hpp"

namespace orm::db::commands
{
class DefaultDropTableCommand : public DropTableCommand
{
public:
    [[nodiscard]] auto dropTable(const model::ModelInfo& modelInfo) const -> std::string override;
};
} // namespace orm::db::commands
