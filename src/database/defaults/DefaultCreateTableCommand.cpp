#include "DefaultCreateTableCommand.hpp"

#include <format>

#include "orm-cxx/utils/StringUtils.hpp"

namespace orm::db::commands
{
DefaultCreateTableCommand::DefaultCreateTableCommand(std::shared_ptr<TypeTranslator> typeTranslatorInit)
    : typeTranslator(std::move(typeTranslatorInit))
{
}

auto DefaultCreateTableCommand::createTable(const model::ModelInfo& modelInfo) const -> std::string
{
    std::string command = std::format("CREATE TABLE IF NOT EXISTS {} (\n", modelInfo.tableName);

    const auto& columns = modelInfo.columnsInfo;

    for (const auto& column : columns)
    {
        if (column.isForeignModel)
        {
            command.append(addColumnsForForeignIds(modelInfo.foreignModelsInfo.at(column.name), column));

            continue;
        }

        command.append(std::format("\t{} {}{},\n", column.name, typeTranslator->toSqlType(column.type),
                                   column.isNotNull ? " NOT NULL" : ""));
    }

    if (modelInfo.idColumnsNames.empty())
    {
        utils::removeLastComma(command);
    }
    else
    {
        command.append("\tPRIMARY KEY (");

        for (const auto& idColumn : modelInfo.idColumnsNames)
        {
            command.append(std::format("{}, ", idColumn));
        }

        utils::removeLastComma(command);

        command.append(")");
    }

    if (not modelInfo.foreignModelsInfo.empty())
    {
        command.append(std::format(",\n{}", addForeignIds(modelInfo)));
    }

    command.append("\n);");

    return command;
}

auto DefaultCreateTableCommand::addColumnsForForeignIds(const model::ModelInfo& modelInfo,
                                                        const model::ColumnInfo& columnInfo) const -> std::string
{
    std::string command;

    for (const auto& idColumn : modelInfo.idColumnsNames)
    {
        command.append(std::format("\t{} {}{},\n", idColumn, typeTranslator->toSqlType(columnInfo.type),
                                   columnInfo.isNotNull ? " NOT NULL" : ""));
    }

    return command;
}

auto DefaultCreateTableCommand::addForeignIds(const model::ModelInfo& modelInfo) -> std::string
{
    std::string command;

    for (const auto& [_, foreignModelInfo] : modelInfo.foreignModelsInfo)
    {
        for (const auto& idColumn : foreignModelInfo.idColumnsNames)
        {
            command.append(
                std::format("\tFOREIGN KEY ({}) REFERENCES {}({}),\n", idColumn, foreignModelInfo.tableName, idColumn));
        }
    }

    utils::removeLastComma(command);

    return command;
}

} // namespace orm::db::commands
