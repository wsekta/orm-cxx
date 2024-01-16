#include "SqliteCommandGenerator.hpp"

#include <format>

namespace orm::db::sqlite
{
auto SqliteCommandGenerator::createTable(const model::ModelInfo& modelInfo) const -> std::string
{
    std::string command = std::format("CREATE TABLE IF NOT EXISTS {} (\n", modelInfo.tableName);

    auto& columns = modelInfo.columnsInfo;

    for (auto& column : columns)
    {
        if (column.isForeignModel)
        {
            command.append(addColumnsForForeignIds(modelInfo.foreignModelsInfo.at(column.name), column));

            continue;
        }

        command.append(std::format("\t{} {}{},\n", column.name, typeTranslator.toSqlType(column.type),
                                   column.isNotNull ? " NOT NULL" : ""));
    }

    if (modelInfo.idColumnsNames.empty())
    {
        removeLastComma(command);
    }
    else
    {
        command.append("\tPRIMARY KEY (");

        for (auto& idColumn : modelInfo.idColumnsNames)
        {
            command.append(std::format("{}, ", idColumn));
        }

        removeLastComma(command);

        command.append(")");
    }

    if (not modelInfo.foreignModelsInfo.empty())
    {
        command.append(std::format(",\n{}", addForeignIds(modelInfo)));
    }

    command.append("\n);");

    return command;
}

auto SqliteCommandGenerator::dropTable(const model::ModelInfo& modelInfo) const -> std::string
{
    return std::format("DROP TABLE IF EXISTS {};", modelInfo.tableName);
}

auto SqliteCommandGenerator::insert(const model::ModelInfo& modelInfo) const -> std::string
{
    std::string command = std::format("INSERT INTO {} (", modelInfo.tableName);

    auto columns = modelInfo.columnsInfo;

    for (auto& column : columns)
    {
        command.append(std::format("{}, ", column.name));
    }

    removeLastComma(command);

    command.append(") VALUES (");

    for (auto& column : columns)
    {
        command.append(std::format(":{}, ", column.name));
    }

    removeLastComma(command);

    command.append(");");

    return command;
}

auto SqliteCommandGenerator::addColumnsForForeignIds(const model::ModelInfo& modelInfo, 
                                                     const model::ColumnInfo& columnInfo) const -> std::string
{
    std::string command{};

    const auto& fieldName = columnInfo.name;
    const auto isNullable = columnInfo.isNotNull ? " NOT NULL" : "";

    for (const auto& foreignColumnInfo : modelInfo.columnsInfo)
    {
        if (foreignColumnInfo.isPrimaryKey)
        {
            command.append(std::format("\t{}_{} {}{},\n", fieldName, foreignColumnInfo.name,
                                    typeTranslator.toSqlType(foreignColumnInfo.type), isNullable));
        }
    }

    return command;
}

auto SqliteCommandGenerator::addForeignIds(const model::ModelInfo& modelInfo) const -> std::string
{
    std::string command{};

    for (const auto& [fieldName, foreignModelInfo] : modelInfo.foreignModelsInfo)
    {
        std::string foreignKeyCommand{"\tFOREIGN KEY ("};
        for (const auto& foreignCoulmnInfo : foreignModelInfo.columnsInfo)
        {
            if (foreignCoulmnInfo.isPrimaryKey)
            {
                foreignKeyCommand.append(std::format("{}_{}, ", fieldName, foreignCoulmnInfo.name));
            }
        }

        removeLastComma(foreignKeyCommand);

        foreignKeyCommand.append(std::format(") REFERENCES {} (", foreignModelInfo.tableName));

        for (const auto& foreignIdName : foreignModelInfo.idColumnsNames)
        {
            foreignKeyCommand.append(std::format("{}, ", foreignIdName));
        }

        removeLastComma(foreignKeyCommand);

        foreignKeyCommand.append("),\n");

        command.append(foreignKeyCommand);
    }

    if (not command.empty())
    {
        removeLastComma(command);
    }

    return command;
}
}