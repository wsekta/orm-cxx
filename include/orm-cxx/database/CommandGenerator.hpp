#pragma once

#include <string>

#include "commands/CreateTableCommand.hpp"
#include "commands/DeleteCommand.hpp"
#include "commands/DropTableCommand.hpp"
#include "commands/InsertCommand.hpp"
#include "commands/SelectCommand.hpp"
#include "commands/UpdateCommand.hpp"
#include "orm-cxx/model/ModelView.hpp"
#include "orm-cxx/query/SelectSpec.hpp"
#include "orm-cxx/query/UpdateSpec.hpp"
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

    [[nodiscard]] auto createTable(model::ModelView model) const -> std::string;
    [[nodiscard]] auto dropTable(model::ModelView model) const -> std::string;
    [[nodiscard]] auto insert(model::ModelView model) const -> std::string;
    [[nodiscard]] auto select(model::ModelView model, const query::SelectSpec& spec) const -> SelectStatement;
    [[nodiscard]] auto update(model::ModelView model, const query::UpdateSpec& spec) const -> Statement;
    [[nodiscard]] auto remove(model::ModelView model, const query::Predicate& predicate) const -> Statement;

private:
    std::unique_ptr<commands::CreateTableCommand> createTableCommand;
    std::unique_ptr<commands::DropTableCommand> dropTableCommand;
    std::unique_ptr<commands::InsertCommand> insertCommand;
    std::unique_ptr<commands::SelectCommand> selectCommand;
    std::unique_ptr<commands::UpdateCommand> updateCommand;
    std::unique_ptr<commands::DeleteCommand> deleteCommand;
};
} // namespace orm::db
