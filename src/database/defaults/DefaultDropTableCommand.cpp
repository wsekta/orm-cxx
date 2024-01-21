#include "DefaultDropTableCommand.hpp"

namespace orm::db::commands
{
auto DefaultDropTableCommand::dropTable(const model::ModelInfo& modelInfo) const -> std::string
{
    return "DROP TABLE IF EXISTS " + modelInfo.tableName + ";";
}
} // namespace orm::db::commands
