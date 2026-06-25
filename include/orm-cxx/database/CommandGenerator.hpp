#pragma once

#include <string>

#include "commands/CreateTableCommand.hpp"
#include "commands/DeleteCommand.hpp"
#include "commands/DropTableCommand.hpp"
#include "commands/InsertCommand.hpp"
#include "commands/SelectCommand.hpp"
#include "commands/UpdateCommand.hpp"
#include "orm-cxx/model/ModelInfo.hpp"
#include "orm-cxx/query/QueryData.hpp"
#include "SelectStatement.hpp"

namespace orm::db
{
class CommandGenerator
{
public:
    CommandGenerator(std::unique_ptr<commands::CreateTableCommand> createTableCommand,
                     std::unique_ptr<commands::DropTableCommand> dropTableCommand,
                     std::unique_ptr<commands::InsertCommand> insertCommand,
                     std::unique_ptr<commands::SelectCommand> selectCommand,
                     std::unique_ptr<commands::UpdateCommand> updateCommand,
                     std::unique_ptr<commands::DeleteCommand> deleteCommand);

    [[nodiscard]] auto createTable(const model::ModelInfo& modelInfo) const -> std::string;
    [[nodiscard]] auto dropTable(const model::ModelInfo& modelInfo) const -> std::string;
    [[nodiscard]] auto insert(const model::ModelInfo& modelInfo) const -> std::string;
    [[nodiscard]] auto select(const query::QueryData& queryData) const -> SelectStatement;
    [[nodiscard]] auto update(const query::UpdateData& updateData) const -> Statement;
    [[nodiscard]] auto remove(const model::ModelInfo& modelInfo, const query::Predicate& predicate) const -> Statement;

private:
    std::unique_ptr<commands::CreateTableCommand> createTableCommand;
    std::unique_ptr<commands::DropTableCommand> dropTableCommand;
    std::unique_ptr<commands::InsertCommand> insertCommand;
    std::unique_ptr<commands::SelectCommand> selectCommand;
    std::unique_ptr<commands::UpdateCommand> updateCommand;
    std::unique_ptr<commands::DeleteCommand> deleteCommand;
};
} // namespace orm::db
