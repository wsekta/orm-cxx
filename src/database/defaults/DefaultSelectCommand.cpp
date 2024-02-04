#include "DefaultSelectCommand.hpp"

#include <format>

#include "orm-cxx/utils/StringUtils.hpp"

using orm::utils::removeLastComma;

namespace orm::db::commands
{
auto DefaultSelectCommand::select(const query::QueryData& queryData) const -> std::string
{
    return std::format("SELECT {0:} FROM {1:}{2:}{3:}{4:};", getSelectFields(queryData.shouldJoin, queryData.modelInfo),
                       queryData.modelInfo.tableName, getJoins(queryData.shouldJoin, queryData.modelInfo),
                       getOffset(queryData.offset), getLimit(queryData.limit));
}

auto DefaultSelectCommand::getSelectFields(bool shouldJoin, const model::ModelInfo& modelInfo) -> std::string
{
    std::string selectFields;

    for (const auto& columnInfo : modelInfo.columnsInfo)
    {
        if (columnInfo.isForeignModel)
        {
            selectFields += getForeignModelSelectFields(shouldJoin, columnInfo.name,
                                                        modelInfo.foreignModelsInfo.at(columnInfo.name), modelInfo);
        }
        else
        {
            selectFields += std::format("{0:}.{1:} AS {0:}_{1:}, ", modelInfo.tableName, columnInfo.name);
        }
    }

    removeLastComma(selectFields);

    return selectFields;
}

auto DefaultSelectCommand::getForeignModelSelectFields(bool shouldJoin, const std::string& foreignModelFieldName,
                                                       const model::ModelInfo& foreignModelInfo,
                                                       const model::ModelInfo& modelInfo) -> std::string
{

    std::string selectFields;
    if (shouldJoin)
    {
        for (const auto& columnInfo : foreignModelInfo.columnsInfo)
        {
            selectFields += std::format("{0:}.{1:} AS {0:}_{1:}, ", foreignModelFieldName, columnInfo.name);
        }
    }
    else
    {
        for (const auto& columnInfo : foreignModelInfo.columnsInfo)
        {
            if (columnInfo.isPrimaryKey)
            {
                selectFields += std::format("{2:}.{0:}_{1:} AS {2:}_{0:}_{1:}, ", foreignModelFieldName,
                                            columnInfo.name, modelInfo.tableName);
            }
        }
    }

    return selectFields;
}

auto DefaultSelectCommand::getJoins(bool shouldJoin, const model::ModelInfo& modelInfo) -> std::string
{
    if (not shouldJoin)
    {
        return {};
    }

    std::string joins;

    for (const auto& [foreginModelFieldName, foreignModelInfo] : modelInfo.foreignModelsInfo)
    {
        joins += std::format(" LEFT JOIN {0:} AS {3:} ON {3:}.{1:} = {2}.{3:}_{1:} ", foreignModelInfo.tableName,
                             *foreignModelInfo.idColumnsNames.begin(), // todo: fix this for multiple id columns
                             modelInfo.tableName, foreginModelFieldName);
    }

    if (not joins.empty())
    {
        joins.pop_back();
    }

    return joins;
}

auto DefaultSelectCommand::getOffset(const std::optional<std::size_t>& offset) -> std::string
{
    if (offset.has_value())
    {
        return std::format(" OFFSET {}", offset.value());
    }

    return {};
}

auto DefaultSelectCommand::getLimit(const std::optional<std::size_t>& limit) -> std::string
{
    if (limit.has_value())
    {
        return std::format(" LIMIT {}", limit.value());
    }

    return {};
}

auto DefaultSelectCommand::getWhere(const query::Condition& condition) -> std::string
{
    switch (condition.getOperatorType())
    {
    case orm::query::Operator::LIKE:
        return std::format(" WHERE {} LIKE {}", condition.getColumnName(), condition.getComparisonValue());
    case orm::query::Operator::NOT_LIKE:
        return std::format(" WHERE {} NOT LIKE {}", condition.getColumnName(), condition.getComparisonValue());
    case orm::query::Operator::IS_NULL:
        return std::format(" WHERE {} IS NULL", condition.getColumnName());
    case orm::query::Operator::IS_NOT_NULL:
        return std::format(" WHERE {} IS NOT NULL", condition.getColumnName());
    default:
        return {};
    }
}
} // namespace orm::db::commands
