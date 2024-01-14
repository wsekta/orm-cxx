#include "orm-cxx/database/CommandGeneratorFactory.hpp"

#include "sqlite/SqliteCommandGenerator.hpp"

namespace orm::db
{
CommandGeneratorFactory::CommandGeneratorFactory()
{
    commandGenerators.emplace(BackendType::Sqlite, std::make_unique<sqlite::SqliteCommandGenerator>());
}

auto CommandGeneratorFactory::getCommandGenerator(BackendType backendType) const
    -> const std::unique_ptr<CommandGenerator>&
{
    return commandGenerators.at(backendType);
}

auto CommandGenerator::removeLastComma(std::string& command) const -> void
{
    command.pop_back();
    command.pop_back();
}
}