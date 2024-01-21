#pragma once

#include <string>

#include "commands/CreateTableCommand.hpp"
#include "commands/DropTableCommand.hpp"
#include "commands/InsertCommand.hpp"
#include "commands/SelectCommand.hpp"
#include "orm-cxx/model/ModelInfo.hpp"
#include "orm-cxx/query/QueryData.hpp"

namespace orm::db
{
class CommandGenerator
{
public:
    CommandGenerator(std::unique_ptr<commands::CreateTableCommand> createTableCommand,
                     std::unique_ptr<commands::DropTableCommand> dropTableCommand,
                     std::unique_ptr<commands::InsertCommand> insertCommand,
                     std::unique_ptr<commands::SelectCommand> selectCommand);

    [[nodiscard]] auto createTable(const model::ModelInfo& modelInfo) const -> std::string;
    [[nodiscard]] auto dropTable(const model::ModelInfo& modelInfo) const -> std::string;
    [[nodiscard]] auto insert(const model::ModelInfo& modelInfo) const -> std::string;
    [[nodiscard]] auto select(const query::QueryData& queryData) const -> std::string;

private:
    std::unique_ptr<commands::CreateTableCommand> createTableCommand;
    std::unique_ptr<commands::DropTableCommand> dropTableCommand;
    std::unique_ptr<commands::InsertCommand> insertCommand;
    std::unique_ptr<commands::SelectCommand> selectCommand;
};
} // namespace orm::db
