#pragma once

#include "orm-cxx/database/commands/CreateTableCommand.hpp"
#include "orm-cxx/database/SqlDialect.hpp"

namespace orm::db::commands
{
class DefaultCreateTableCommand : public CreateTableCommand
{
public:
    explicit DefaultCreateTableCommand(const SqlDialect& dialect);

    [[nodiscard]] auto createTable(model::ModelView model) const -> std::string override;

private:
    const SqlDialect& dialect;

    [[nodiscard]] auto addColumnsForForeignIds(model::ModelView target,
                                               const model::ColumnView& column) const -> std::string;
    [[nodiscard]] static auto hasAutoIncrementPrimaryKey(model::ModelView model) -> bool;
    [[nodiscard]] auto addForeignIds(model::ModelView model) const -> std::string;
};
} // namespace orm::db::commands
