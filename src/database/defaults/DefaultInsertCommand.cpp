#include "DefaultInsertCommand.hpp"

#include <format>

#include "orm-cxx/utils/StringUtils.hpp"

namespace orm::db::commands
{
auto DefaultInsertCommand::insert(const model::ModelInfo& modelInfo) const -> std::string
{
    std::string command = std::format("INSERT INTO {} (", modelInfo.tableName);

    auto columns = modelInfo.columnsInfo;

    for (auto& column : columns)
    {
        command.append(std::format("{}, ", column.name));
    }

    utils::removeLastComma(command);

    command.append(") VALUES (");

    for (auto& column : columns)
    {
        command.append(std::format(":{}, ", column.name));
    }

    utils::removeLastComma(command);

    command.append(");");

    return command;
}
} // namespace orm::db::commands
