#include "DefaultDropTableCommand.hpp"

namespace orm::db::commands
{
DefaultDropTableCommand::DefaultDropTableCommand(const SqlDialect& dialectInit) : dialect{dialectInit} {}

auto DefaultDropTableCommand::dropTable(model::ModelView model) const -> std::string
{
    return dialect.renderDropTable(model->tableName, true);
}
} // namespace orm::db::commands
