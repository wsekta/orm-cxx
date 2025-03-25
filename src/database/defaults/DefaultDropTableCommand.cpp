#include "DefaultDropTableCommand.hpp"

#include <format>

namespace orm::db::commands
{
auto DefaultDropTableCommand::dropTable(const model::ModelInfo& modelInfo) const -> std::string
{
    return std::format("DROP TABLE IF EXISTS {};", modelInfo.tableName);
}
} // namespace orm::db::commands
