#include "DefaultSelectCommand.hpp"

#include <format>
#include <stdexcept>
#include <string_view>
#include <variant>
#include <vector>

#include "orm-cxx/utils/StringUtils.hpp"
#include "SqlAliases.hpp"

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

auto comparisonOperatorToSql(orm::query::ComparisonOperator comparisonOperator) -> std::string_view
{
    switch (comparisonOperator)
    {
    case orm::query::ComparisonOperator::Equal:
        return "=";
    case orm::query::ComparisonOperator::NotEqual:
        return "!=";
    case orm::query::ComparisonOperator::Greater:
        return ">";
    case orm::query::ComparisonOperator::GreaterOrEqual:
        return ">=";
    case orm::query::ComparisonOperator::Less:
        return "<";
    case orm::query::ComparisonOperator::LessOrEqual:
        return "<=";
    case orm::query::ComparisonOperator::Like:
    case orm::query::ComparisonOperator::NotLike:
        break;
    }

    throw std::invalid_argument{"Unsupported aggregate comparison operator"};
}

auto aggregateFunctionToSql(orm::query::AggregateFunction function) -> std::string_view
{
    switch (function)
    {
    case orm::query::AggregateFunction::Count:
    case orm::query::AggregateFunction::CountAll:
        return "COUNT";
    case orm::query::AggregateFunction::Sum:
        return "SUM";
    case orm::query::AggregateFunction::Avg:
        return "AVG";
    case orm::query::AggregateFunction::Min:
        return "MIN";
    case orm::query::AggregateFunction::Max:
        return "MAX";
    }

    throw std::invalid_argument{"Unsupported aggregate function"};
}

template <class... Ts>
struct Overloaded : Ts...
{
    using Ts::operator()...;
};

template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;
} // namespace

namespace orm::db::commands
{
DefaultSelectCommand::DefaultSelectCommand(const SqlDialect& dialectInit) : dialect{dialectInit} {}

auto DefaultSelectCommand::select(const query::QueryData& queryData) const -> SelectStatement
{
    RenderContext context{
        .modelInfo = queryData.modelInfo,
        .dialect = dialect,
        .shouldJoin = queryData.shouldJoin,
        .columnRenderMode = ColumnRenderMode::Select,
    };
    const auto selectFields = getSelectFields(queryData, context);
    const auto joins = getJoins(queryData.shouldJoin, queryData.modelInfo, dialect);
    const auto where = renderWhere(queryData.predicate, context);
    const auto groupBy = getGroupBy(queryData, context);
    const auto having = getHaving(queryData.having, context);
    const auto orderBy = getOrderBy(queryData, context);
    const auto pagination = dialect.renderPagination(PaginationSpec{
        .limit = queryData.limit,
        .offset = queryData.offset,
        .hasOrderBy = not queryData.orderBy.empty(),
    });

    const auto sql = std::format("{} {} FROM {}{}{}{}{}{}{};", queryData.isDistinct ? "SELECT DISTINCT" : "SELECT",
                                 selectFields, dialect.quoteIdentifier(queryData.modelInfo.tableName), joins, where,
                                 groupBy, having, orderBy, pagination);

    return SelectStatement{.sql = sql, .parameters = std::move(context.parameters)};
}

auto DefaultSelectCommand::getSelectFields(const query::QueryData& queryData, RenderContext& context) -> std::string
{
    if (queryData.projections.empty())
    {
        return getFullModelSelectFields(queryData.shouldJoin, queryData.modelInfo, context.dialect);
    }

    return getProjectionSelectFields(queryData.projections, context);
}

auto DefaultSelectCommand::getFullModelSelectFields(bool shouldJoin, const model::ModelInfo& modelInfo,
                                                    const SqlDialect& dialect) -> std::string
{
    std::string selectFields;

    for (const auto& columnInfo : modelInfo.columnsInfo)
    {
        if (columnInfo.isForeignModel)
        {
            selectFields += getForeignModelSelectFields(
                shouldJoin, columnInfo.name, modelInfo.foreignModelsInfo.at(columnInfo.name), modelInfo, dialect);
        }
        else
        {
            selectFields +=
                std::format("{} AS {}, ", aliases::qualifiedIdentifier(dialect, modelInfo.tableName, columnInfo.name),
                            dialect.quoteIdentifier(aliases::modelColumn(modelInfo.tableName, columnInfo.name)));
        }
    }

    removeLastComma(selectFields);

    return selectFields;
}

auto DefaultSelectCommand::getProjectionSelectFields(const std::vector<query::Projection>& projections,
                                                     RenderContext& context) -> std::string
{
    std::vector<std::string> selectFields;
    selectFields.reserve(projections.size());

    for (const auto& projection : projections)
    {
        selectFields.push_back(std::format("{} AS {}", renderProjectionSource(projection.source, context),
                                           context.dialect.quoteIdentifier(projection.resultField)));
    }

    return join(selectFields, ", ");
}

auto DefaultSelectCommand::renderProjectionSource(const query::ProjectionSource& source, RenderContext& context)
    -> std::string
{
    return std::visit(Overloaded{[&context](const query::Column& column) { return renderColumn(column, context); },
                                 [&context](const query::AggregateExpression& aggregate)
                                 {
                                     const auto expression = renderAggregate(aggregate, context);
                                     const auto returnsExactNumeric =
                                         aggregate.function == query::AggregateFunction::Sum or
                                         aggregate.function == query::AggregateFunction::Avg;
                                     return context.dialect.renderAggregateResult(expression, returnsExactNumeric);
                                 }},
                      source);
}

auto DefaultSelectCommand::renderAggregate(const query::AggregateExpression& aggregate, RenderContext& context)
    -> std::string
{
    const auto functionName = aggregateFunctionToSql(aggregate.function);

    if (aggregate.function == query::AggregateFunction::CountAll)
    {
        return std::format("{}(*)", functionName);
    }

    if (not aggregate.column.has_value())
    {
        throw std::invalid_argument{"Aggregate function requires a source column"};
    }

    return std::format("{}({})", functionName, renderColumn(aggregate.column.value(), context));
}

auto DefaultSelectCommand::getForeignModelSelectFields(bool shouldJoin, const std::string& foreignModelFieldName,
                                                       const model::ModelInfo& foreignModelInfo,
                                                       const model::ModelInfo& modelInfo, const SqlDialect& dialect)
    -> std::string
{

    std::string selectFields;
    if (shouldJoin)
    {
        for (const auto& columnInfo : foreignModelInfo.columnsInfo)
        {
            selectFields += std::format(
                "{} AS {}, ", aliases::qualifiedIdentifier(dialect, foreignModelFieldName, columnInfo.name),
                dialect.quoteIdentifier(aliases::joinedRelationColumn(foreignModelFieldName, columnInfo.name)));
        }
    }
    else
    {
        for (const auto& columnInfo : foreignModelInfo.columnsInfo)
        {
            if (columnInfo.isPrimaryKey)
            {
                selectFields += std::format(
                    "{} AS {}, ",
                    aliases::qualifiedIdentifier(dialect, modelInfo.tableName,
                                                 aliases::joinedRelationColumn(foreignModelFieldName, columnInfo.name)),
                    dialect.quoteIdentifier(
                        aliases::unjoinedRelationColumn(modelInfo.tableName, foreignModelFieldName, columnInfo.name)));
            }
        }
    }

    return selectFields;
}

auto DefaultSelectCommand::getJoins(bool shouldJoin, const model::ModelInfo& modelInfo, const SqlDialect& dialect)
    -> std::string
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
                joinPredicates.push_back(std::format(
                    "{} = {}", aliases::qualifiedIdentifier(dialect, columnInfo.name, foreignColumnInfo.name),
                    aliases::qualifiedIdentifier(
                        dialect, modelInfo.tableName,
                        aliases::joinedRelationColumn(columnInfo.name, foreignColumnInfo.name))));
            }
        }

        joins += std::format(" LEFT JOIN {} AS {} ON {} ", dialect.quoteIdentifier(foreignModelInfo.tableName),
                             dialect.quoteIdentifier(columnInfo.name), join(joinPredicates, " AND "));
    }

    if (not joins.empty())
    {
        joins.pop_back();
    }

    return joins;
}

auto DefaultSelectCommand::getGroupBy(const query::QueryData& queryData, RenderContext& context) -> std::string
{
    if (queryData.groupBy.empty())
    {
        return {};
    }

    std::vector<std::string> groupByClauses;
    groupByClauses.reserve(queryData.groupBy.size());

    for (const auto& column : queryData.groupBy)
    {
        groupByClauses.push_back(renderColumn(column, context));
    }

    return " GROUP BY " + join(groupByClauses, ", ");
}

auto DefaultSelectCommand::getHaving(const std::optional<query::AggregatePredicate>& having, RenderContext& context)
    -> std::string
{
    if (not having.has_value())
    {
        return {};
    }

    return " HAVING " + renderAggregatePredicate(having->getNode(), context);
}

auto DefaultSelectCommand::renderAggregatePredicate(const query::AggregatePredicateNodePtr& node,
                                                    RenderContext& context) -> std::string
{
    return renderAggregatePredicate(*node, context);
}

auto DefaultSelectCommand::renderAggregatePredicate(const query::AggregatePredicateNode& node, RenderContext& context)
    -> std::string
{
    return std::visit(
        Overloaded{[&context](const query::AggregateComparisonExpression& expression)
                   {
                       const auto aggregate = renderAggregate(expression.aggregate, context);
                       const auto sqlOperator = comparisonOperatorToSql(expression.comparisonOperator);
                       const auto parameter = addAutomaticParameter(context, expression.value);

                       return std::format("{} {} {}", aggregate, sqlOperator, parameter);
                   },
                   [&context](const query::AggregateLogicalExpression& expression)
                   {
                       const auto left = renderAggregatePredicate(expression.left, context);
                       const auto sqlOperator =
                           expression.logicalOperator == query::LogicalOperator::And ? "AND" : "OR";
                       const auto right = renderAggregatePredicate(expression.right, context);

                       return std::format("({} {} {})", left, sqlOperator, right);
                   },
                   [&context](const query::AggregateNotExpression& expression)
                   { return std::format("(NOT ({}))", renderAggregatePredicate(expression.predicate, context)); }},
        node.expression);
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
