#include "orm-cxx/database/CommandGenerator.hpp"

namespace orm::db
{
CommandGenerator::CommandGenerator(std::unique_ptr<commands::CreateTableCommand> createTableCommandInit,
                                   std::unique_ptr<commands::DropTableCommand> dropTableCommandInit,
                                   std::unique_ptr<commands::InsertCommand> insertCommandInit,
                                   std::unique_ptr<commands::SelectCommand> selectCommandInit)
    : createTableCommand(std::move(createTableCommandInit)),
      dropTableCommand(std::move(dropTableCommandInit)),
      insertCommand(std::move(insertCommandInit)),
      selectCommand(std::move(selectCommandInit))
{
}

auto CommandGenerator::createTable(const model::ModelInfo& modelInfo) const -> std::string
{
    return createTableCommand->createTable(modelInfo);
}

auto CommandGenerator::dropTable(const model::ModelInfo& modelInfo) const -> std::string
{
    return dropTableCommand->dropTable(modelInfo);
}

auto CommandGenerator::insert(const model::ModelInfo& modelInfo) const -> std::string
{
    return insertCommand->insert(modelInfo);
}

auto CommandGenerator::select(const query::QueryData& queryData) const -> std::string
{
    return selectCommand->select(queryData);
}
} // namespace orm::db
