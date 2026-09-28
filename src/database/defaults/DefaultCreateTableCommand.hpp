#pragma once

#include <vector>

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
    struct ForeignKeyTarget
    {
        model::ColumnView relationColumn;
        model::ModelView target;
    };

    const SqlDialect& dialect;

    [[nodiscard]] auto addColumnsForForeignIds(model::ModelView target,
                                               const model::ColumnView& column) const -> std::string;
    [[nodiscard]] static auto hasAutoIncrementPrimaryKey(model::ModelView model) -> bool;
    [[nodiscard]] auto addForeignIds(const std::vector<ForeignKeyTarget>& foreignKeys) const -> std::string;
};
} // namespace orm::db::commands
