#include "DefaultSelectCommand.hpp"

#include <format>

#include "orm-cxx/utils/StringUtils.hpp"

using orm::utils::removeLastComma;

namespace orm::db::commands
{
auto DefaultSelectCommand::select(const query::QueryData& queryData) const -> std::string
{
    return std::format("SELECT {0:} FROM {1:}{2:}{3:}{4:};", getSelectFields(queryData.modelInfo),
                       queryData.modelInfo.tableName, getJoins(queryData.shouldJoin, queryData.modelInfo),
                       getOffset(queryData.offset), getLimit(queryData.limit));
}

auto DefaultSelectCommand::getSelectFields(const model::ModelInfo& modelInfo) -> std::string
{
    std::string selectFields;

    for (const auto& columnInfo : modelInfo.columnsInfo)
    {
        if (columnInfo.isForeignModel)
        {
            selectFields += getForeignModelSelectFields(std::format("{}.{}", modelInfo.tableName, columnInfo.name),
                                                        modelInfo.foreignModelsInfo.at(columnInfo.name));
        }
        else
        {
            selectFields += std::format("{}.{}, ", modelInfo.tableName, columnInfo.name);
        }
    }

    removeLastComma(selectFields);

    return selectFields;
}

auto DefaultSelectCommand::getForeignModelSelectFields(const std::string& foreignModelFieldName,
                                                       const model::ModelInfo& foreignModelInfo) -> std::string
{
    std::string selectFields;

    for (const auto& columnInfo : foreignModelInfo.columnsInfo)
    {
        if (columnInfo.isPrimaryKey)
        {
            selectFields += std::format("{}_{}, ", foreignModelFieldName, columnInfo.name);
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
        joins += std::format(" LEFT JOIN {0:} AS {1:} ON {1:}.{2:} = {3:}.{4:}_{2:} ", foreignModelInfo.tableName,
                             foreginModelFieldName,
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
} // namespace orm::db::commands
