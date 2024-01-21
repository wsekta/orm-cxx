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
    std::string command{};

    const auto& fieldName = columnInfo.name;
    const auto* const isNullable = columnInfo.isNotNull ? " NOT NULL" : "";

    for (const auto& foreignColumnInfo : modelInfo.columnsInfo)
    {
        if (foreignColumnInfo.isPrimaryKey)
        {
            command.append(std::format("\t{}_{} {}{},\n", fieldName, foreignColumnInfo.name,
                                       typeTranslator->toSqlType(foreignColumnInfo.type), isNullable));
        }
    }

    return command;
}

auto DefaultCreateTableCommand::addForeignIds(const model::ModelInfo& modelInfo) -> std::string
{
    std::string command{};

    for (const auto& [fieldName, foreignModelInfo] : modelInfo.foreignModelsInfo)
    {
        std::string foreignKeyCommand{"\tFOREIGN KEY ("};
        for (const auto& foreignColumnInfo : foreignModelInfo.columnsInfo)
        {
            if (foreignColumnInfo.isPrimaryKey)
            {
                foreignKeyCommand.append(std::format("{}_{}, ", fieldName, foreignColumnInfo.name));
            }
        }

        utils::removeLastComma(foreignKeyCommand);

        foreignKeyCommand.append(std::format(") REFERENCES {} (", foreignModelInfo.tableName));

        for (const auto& foreignIdName : foreignModelInfo.idColumnsNames)
        {
            foreignKeyCommand.append(std::format("{}, ", foreignIdName));
        }

        utils::removeLastComma(foreignKeyCommand);

        foreignKeyCommand.append("),\n");

        command.append(foreignKeyCommand);
    }

    if (not command.empty())
    {
        utils::removeLastComma(command);
    }

    return command;
}

} // namespace orm::db::commands
