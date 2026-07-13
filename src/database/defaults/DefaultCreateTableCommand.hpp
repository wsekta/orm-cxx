#pragma once

#include "orm-cxx/database/commands/CreateTableCommand.hpp"
#include "orm-cxx/database/SqlDialect.hpp"

namespace orm::db::commands
{
class DefaultCreateTableCommand : public CreateTableCommand
{
public:
    explicit DefaultCreateTableCommand(const SqlDialect& dialect);

    [[nodiscard]] auto createTable(const model::ModelInfo& modelInfo) const -> std::string override;

private:
    const SqlDialect& dialect;

    [[nodiscard]] auto addColumnsForForeignIds(const model::ModelInfo& modelInfo,
                                               const model::ColumnInfo& columnInfo) const -> std::string;
    [[nodiscard]] static auto hasAutoIncrementPrimaryKey(const model::ModelInfo& modelInfo) -> bool;
    [[nodiscard]] auto addForeignIds(const model::ModelInfo& modelInfo) const -> std::string;
};
} // namespace orm::db::commands
