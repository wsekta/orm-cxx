#include "DefaultSelectCommand.hpp"

#include <format>
#include <string_view>
#include <vector>

#include "orm-cxx/utils/StringUtils.hpp"

using orm::utils::removeLastComma;

namespace
{
auto join(const std::vector<std::string>& parts, std::string_view separator) -> std::string
{
    std::string joined;

    for (const auto& part : parts)
    {
        joined += part;
        joined += separator;
    }

    if (not joined.empty())
    {
        joined.resize(joined.size() - separator.size());
    }

    return joined;
}
} // namespace

namespace orm::db::commands
{
auto DefaultSelectCommand::select(const query::QueryData& queryData) const -> SelectStatement
{
    RenderContext context{
        .modelInfo = queryData.modelInfo,
        .shouldJoin = queryData.shouldJoin,
        .columnRenderMode = ColumnRenderMode::Select,
    };
    const auto selectFields = getSelectFields(queryData.shouldJoin, queryData.modelInfo);
    const auto joins = getJoins(queryData.shouldJoin, queryData.modelInfo);
    const auto where = renderWhere(queryData.predicate, context);
    const auto orderBy = getOrderBy(queryData, context);
    const auto limit = getLimit(queryData.limit);
    const auto offset = getOffset(queryData.offset);

    const auto sql = std::format("{} {} FROM {}{}{}{}{}{};", queryData.isDistinct ? "SELECT DISTINCT" : "SELECT",
                                 selectFields, queryData.modelInfo.tableName, joins, where, orderBy, limit, offset);

    return SelectStatement{.sql = sql, .parameters = std::move(context.parameters)};
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

    for (const auto& columnInfo : modelInfo.columnsInfo)
    {
        if (not columnInfo.isForeignModel)
        {
            continue;
        }

        const auto& foreignModelInfo = modelInfo.foreignModelsInfo.at(columnInfo.name);
        std::vector<std::string> joinPredicates;

        for (const auto& foreignColumnInfo : foreignModelInfo.columnsInfo)
        {
            if (foreignColumnInfo.isPrimaryKey)
            {
                joinPredicates.push_back(std::format("{0:}.{1:} = {2:}.{3:}_{1:}", columnInfo.name,
                                                     foreignColumnInfo.name, modelInfo.tableName, columnInfo.name));
            }
        }

        joins += std::format(" LEFT JOIN {0:} AS {1:} ON {2:} ", foreignModelInfo.tableName, columnInfo.name,
                             join(joinPredicates, " AND "));
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

auto DefaultSelectCommand::getOrderBy(const query::QueryData& queryData, RenderContext& context) -> std::string
{
    if (queryData.orderBy.empty())
    {
        return {};
    }

    std::vector<std::string> orderByClauses;
    orderByClauses.reserve(queryData.orderBy.size());

    for (const auto& orderBy : queryData.orderBy)
    {
        if (orderBy.isRaw)
        {
            orderByClauses.push_back(orderBy.rawSql);
            continue;
        }

        orderByClauses.push_back(std::format("{} {}", renderColumn(orderBy.column, context),
                                             orderBy.direction == query::OrderDirection::Asc ? "ASC" : "DESC"));
    }

    return " ORDER BY " + join(orderByClauses, ", ");
}
} // namespace orm::db::commands
