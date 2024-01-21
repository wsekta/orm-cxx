#include "orm-cxx/database/CommandGeneratorFactory.hpp"

#include "defaults/DefaultCreateTableCommand.hpp"
#include "defaults/DefaultDropTableCommand.hpp"
#include "defaults/DefaultInsertCommand.hpp"
#include "defaults/DefaultSelectCommand.hpp"
#include "sqlite/SqliteTypeTranslator.hpp"

namespace orm::db
{
CommandGeneratorFactory::CommandGeneratorFactory()
{
    auto sqliteTypeTranslator = std::make_shared<sqlite::SqliteTypeTranslator>();
    auto sqliteCreateTableCommand = std::make_unique<commands::DefaultCreateTableCommand>(sqliteTypeTranslator);
    auto defaultDropTableCommand = std::make_unique<commands::DefaultDropTableCommand>();
    auto defaultInsertCommand = std::make_unique<commands::DefaultInsertCommand>();
    auto defaultSelectCommand = std::make_unique<commands::DefaultSelectCommand>();

    CommandGenerator sqliteCommandGenerator{std::move(sqliteCreateTableCommand), std::move(defaultDropTableCommand),
                                            std::move(defaultInsertCommand), std::move(defaultSelectCommand)};

    commandGenerators.emplace(BackendType::Sqlite, std::move(sqliteCommandGenerator));
}

auto CommandGeneratorFactory::getCommandGenerator(BackendType backendType) const -> const CommandGenerator&
{
    return commandGenerators.at(backendType);
}
} // namespace orm::db
