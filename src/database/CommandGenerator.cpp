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

auto CommandGenerator::createTable(model::ModelView model) const -> std::string
{
    return createTableCommand->createTable(model);
}

auto CommandGenerator::dropTable(model::ModelView model) const -> std::string
{
    return dropTableCommand->dropTable(model);
}

auto CommandGenerator::insert(model::ModelView model) const -> std::string
{
    return insertCommand->insert(model);
}

auto CommandGenerator::select(model::ModelView model, const query::SelectSpec& spec) const -> SelectStatement
{
    return selectCommand->select(model, spec);
}

auto CommandGenerator::update(model::ModelView model, const query::UpdateSpec& spec) const -> Statement
{
    return updateCommand->update(model, spec);
}

auto CommandGenerator::remove(model::ModelView model, const query::Predicate& predicate) const -> Statement
{
    return deleteCommand->remove(model, predicate);
}
} // namespace orm::db
