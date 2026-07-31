#include "DefaultInsertCommand.hpp"

#include <format>
#include <stdexcept>

#include "orm-cxx/utils/StringUtils.hpp"

namespace orm::db::commands
{
DefaultInsertCommand::DefaultInsertCommand(const SqlDialect& dialectInit) : dialect{dialectInit} {}

auto DefaultInsertCommand::insert(model::ModelView model) const -> std::string
{
    auto fieldsNames = getFieldsNames(model);

    if (fieldsNames.empty())
    {
        return std::format("INSERT INTO {} DEFAULT VALUES;", dialect.quoteIdentifier(model->tableName));
    }

    return std::format("INSERT INTO {} ({}) VALUES ({});", dialect.quoteIdentifier(model->tableName),
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

auto DefaultInsertCommand::getFieldsNames(model::ModelView model) -> std::vector<std::string>
{
    std::vector<std::string> fieldsNames;

    for (const auto& column : model->columns)
    {
        if (column.kind == model::FieldKind::ToOne)
        {
            const auto target = model.resolveTarget(column);
            if (target == nullptr)
            {
                throw std::logic_error{"To-one relation target is not available in the schema"};
            }

            const auto foreignModelIdsNames = getForeignModelIdsNames(column.name, *target);

            fieldsNames.insert(fieldsNames.end(), foreignModelIdsNames.begin(), foreignModelIdsNames.end());
        }
        else
        {
            if (column.isAutoIncrement)
            {
                continue;
            }

            fieldsNames.emplace_back(column.name);
        }
    }

    return fieldsNames;
}

auto DefaultInsertCommand::getForeignModelIdsNames(std::string_view foreignModelFieldName,
                                                   model::ModelView target) -> std::vector<std::string>
{
    std::vector<std::string> foreignModelIdsNames;

    for (const auto& column : target->columns)
    {
        if (column.isPrimaryKey)
        {
            foreignModelIdsNames.push_back(std::format("{}_{}", foreignModelFieldName, column.name));
        }
    }

    return foreignModelIdsNames;
}
} // namespace orm::db::commands
