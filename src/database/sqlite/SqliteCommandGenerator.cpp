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
            command.append(addColumnsForForeignIds(modelInfo.foreignIdsInfo, column));

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

    if (not modelInfo.foreignIdsInfo.empty())
    {
        command.append(std::format(",\n{}", addForeignIds(modelInfo.foreignIdsInfo)));
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

auto SqliteCommandGenerator::addColumnsForForeignIds(const model::ForeignIdsInfo& foreignIdsInfo,
                                                     const model::ColumnInfo& columnInfo) const -> std::string
{
    if (not foreignIdsInfo.contains(columnInfo.name))
    {
        throw std::runtime_error("No foreign ids info for column " + columnInfo.name);
    }

    std::string command{};

    const auto& fieldName = columnInfo.name;
    const auto isNullable = columnInfo.isNotNull ? " NOT NULL" : "";

    for (const auto& [foreignIdName, foreignIColumnInfo] : foreignIdsInfo.at(fieldName).idFields)
    {
        command.append(std::format("{}_{} {}{},\n", fieldName, foreignIdName,
                                   typeTranslator.toSqlType(foreignIColumnInfo.type), isNullable));
    }

    return command;
}

auto SqliteCommandGenerator::addForeignIds(const model::ForeignIdsInfo& foreignIdsInfo) const -> std::string
{
    std::string command{};

    for (const auto& [fieldName, foreignIds] : foreignIdsInfo)
    {
        std::string foreignKeyCommand{"FOREIGN KEY("};
        for (const auto& [foreignIdName, foreignIColumnInfo] : foreignIds.idFields)
        {
            foreignKeyCommand.append(std::format("{}_{}, ", fieldName, foreignIdName));
        }

        removeLastComma(foreignKeyCommand);

        foreignKeyCommand.append(std::format(") REFERENCES {}(", foreignIds.tableName));

        for (const auto& [foreignIdName, foreignIColumnInfo] : foreignIds.idFields)
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