#include "DefaultDropTableCommand.hpp"

namespace orm::db::commands
{
DefaultDropTableCommand::DefaultDropTableCommand(const SqlDialect& dialectInit) : dialect{dialectInit} {}

auto DefaultDropTableCommand::dropTable(const model::ModelInfo& modelInfo) const -> std::string
{
    return dialect.renderDropTable(modelInfo.tableName, true);
}
} // namespace orm::db::commands
