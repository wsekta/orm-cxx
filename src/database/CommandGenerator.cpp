#include "orm-cxx/database/CommandGenerator.hpp"

namespace orm::db
{
CommandGenerator::CommandGenerator(std::unique_ptr<commands::CreateTableCommand> createTableCommandInit,
                                   std::unique_ptr<commands::DropTableCommand> dropTableCommandInit,
                                   std::unique_ptr<commands::InsertCommand> insertCommandInit,
                                   std::unique_ptr<commands::SelectCommand> selectCommandInit,
                                   std::unique_ptr<commands::UpdateCommand> updateCommandInit,
                                   std::unique_ptr<commands::DeleteCommand> deleteCommandInit)
    : createTableCommand(std::move(createTableCommandInit)),
      dropTableCommand(std::move(dropTableCommandInit)),
      insertCommand(std::move(insertCommandInit)),
      selectCommand(std::move(selectCommandInit)),
      updateCommand(std::move(updateCommandInit)),
      deleteCommand(std::move(deleteCommandInit))
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

auto CommandGenerator::select(const query::QueryData& queryData) const -> SelectStatement
{
    return selectCommand->select(queryData);
}

auto CommandGenerator::update(const query::UpdateData& updateData) const -> Statement
{
    return updateCommand->update(updateData);
}

auto CommandGenerator::remove(const model::ModelInfo& modelInfo, const query::Predicate& predicate) const -> Statement
{
    return deleteCommand->remove(modelInfo, predicate);
}
} // namespace orm::db
