#pragma once

#include "orm-cxx/database/commands/DropTableCommand.hpp"
#include "orm-cxx/database/SqlDialect.hpp"

namespace orm::db::commands
{
class DefaultDropTableCommand : public DropTableCommand
{
public:
    explicit DefaultDropTableCommand(const SqlDialect& dialect);

    [[nodiscard]] auto dropTable(const model::ModelInfo& modelInfo) const -> std::string override;

private:
    const SqlDialect& dialect;
};
} // namespace orm::db::commands
