#include "DefaultInsertCommand.hpp"

#include <format>

#include "orm-cxx/utils/StringUtils.hpp"

namespace orm::db::commands
{
DefaultInsertCommand::DefaultInsertCommand(const SqlDialect& dialectInit) : dialect{dialectInit} {}

auto DefaultInsertCommand::insert(const model::ModelInfo& modelInfo) const -> std::string
{
    auto fieldsNames = getFieldsNames(modelInfo);

    if (fieldsNames.empty())
    {
        return std::format("INSERT INTO {} DEFAULT VALUES;", dialect.quoteIdentifier(modelInfo.tableName));
    }

    return std::format("INSERT INTO {} ({}) VALUES ({});", dialect.quoteIdentifier(modelInfo.tableName),
                       getInsertFields(fieldsNames), getInsertValues(fieldsNames));
}

auto DefaultInsertCommand::getInsertFields(const std::vector<std::string>& fieldNames) const -> std::string
{
    std::string insertFields;

    for (const auto& fieldName : fieldNames)
    {
        insertFields += std::format("{}, ", dialect.quoteIdentifier(fieldName));
    }

    utils::removeLastComma(insertFields);

    return insertFields;
}

auto DefaultInsertCommand::getInsertValues(const std::vector<std::string>& fieldNames) const -> std::string
{
    std::string insertValues;

    for (const auto& fieldName : fieldNames)
    {
        insertValues += std::format("{}, ", dialect.bindMarker(fieldName));
    }

    utils::removeLastComma(insertValues);

    return insertValues;
}

auto DefaultInsertCommand::getFieldsNames(const model::ModelInfo& modelInfo) -> std::vector<std::string>
{
    std::vector<std::string> fieldsNames;

    for (const auto& columnInfo : modelInfo.columnsInfo)
    {
        if (columnInfo.isForeignModel)
        {
            const auto& foreignModelInfo = modelInfo.foreignModelsInfo.at(columnInfo.name);

            const auto foreignModelIdsNames = getForeginModelIdsNames(columnInfo.name, foreignModelInfo);

            fieldsNames.insert(fieldsNames.end(), foreignModelIdsNames.begin(), foreignModelIdsNames.end());
        }
        else
        {
            if (columnInfo.isAutoIncrement)
            {
                continue;
            }

            fieldsNames.push_back(columnInfo.name);
        }
    }

    return fieldsNames;
}

auto DefaultInsertCommand::getForeginModelIdsNames(const std::string& foreginModelFieldName,
                                                   const model::ModelInfo& modelInfo) -> std::vector<std::string>
{
    std::vector<std::string> foreignModelIdsNames;

    for (const auto& columnInfo : modelInfo.columnsInfo)
    {
        if (columnInfo.isPrimaryKey)
        {
            foreignModelIdsNames.push_back(std::format("{}_{}", foreginModelFieldName, columnInfo.name));
        }
    }

    return foreignModelIdsNames;
}
} // namespace orm::db::commands
