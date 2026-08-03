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

auto DefaultSelectCommand::select(model::ModelView model, const query::SelectSpec& spec) const -> SelectStatement
{
    RenderContext context{
        .model = model,
        .dialect = dialect,
        .shouldJoin = spec.shouldJoin,
        .columnRenderMode = ColumnRenderMode::Select,
    };
    const auto selectFields = getSelectFields(model, spec, context);
    const auto joins = getJoins(spec.shouldJoin, model, dialect);
    const auto where = renderWhere(spec.predicate, context);
    const auto groupBy = getGroupBy(spec, context);
    const auto having = getHaving(spec.having, context);
    const auto orderBy = getOrderBy(spec, context);
    const auto pagination = dialect.renderPagination(PaginationSpec{
        .limit = spec.limit,
        .offset = spec.offset,
        .hasOrderBy = not spec.orderBy.empty(),
    });

    const auto sql =
        std::format("{} {} FROM {}{}{}{}{}{}{};", spec.isDistinct ? "SELECT DISTINCT" : "SELECT", selectFields,
                    dialect.quoteIdentifier(model->tableName), joins, where, groupBy, having, orderBy, pagination);

    return SelectStatement{.sql = sql, .parameters = std::move(context.parameters)};
}

auto DefaultSelectCommand::getSelectFields(model::ModelView model, const query::SelectSpec& spec,
                                           RenderContext& context) -> std::string
{
    if (spec.projections.empty())
    {
        return getFullModelSelectFields(spec.shouldJoin, model, context.dialect);
    }

    return getProjectionSelectFields(spec.projections, context);
}

auto DefaultSelectCommand::getFullModelSelectFields(bool shouldJoin, model::ModelView model,
                                                    const SqlDialect& dialect) -> std::string
{
    std::string selectFields;

    for (const auto& column : model->columns)
    {
        if (column.kind == model::FieldKind::ToOne)
        {
            const auto target = model.resolveTarget(column);
            if (target == nullptr)
            {
                throw std::logic_error{"To-one relation target is not available in the schema"};
            }
            selectFields += getForeignModelSelectFields(shouldJoin, std::string{column.name}, *target, model, dialect);
        }
        else
        {
            selectFields +=
                std::format("{} AS {}, ", aliases::qualifiedIdentifier(dialect, model->tableName, column.name),
                            dialect.quoteIdentifier(aliases::modelColumn(model->tableName, column.name)));
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

auto DefaultSelectCommand::renderProjectionSource(const query::ProjectionSource& source,
                                                  RenderContext& context) -> std::string
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

auto DefaultSelectCommand::renderAggregate(const query::AggregateExpression& aggregate,
                                           RenderContext& context) -> std::string
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
                                                       model::ModelView target, model::ModelView model,
                                                       const SqlDialect& dialect) -> std::string
{

    std::string selectFields;
    if (shouldJoin)
    {
        for (const auto& column : target->columns)
        {
            selectFields +=
                std::format("{} AS {}, ", aliases::qualifiedIdentifier(dialect, foreignModelFieldName, column.name),
                            dialect.quoteIdentifier(aliases::joinedRelationColumn(foreignModelFieldName, column.name)));
        }
    }
    else
    {
        for (const auto& column : target->columns)
        {
            if (column.isPrimaryKey)
            {
                selectFields += std::format(
                    "{} AS {}, ",
                    aliases::qualifiedIdentifier(dialect, model->tableName,
                                                 aliases::joinedRelationColumn(foreignModelFieldName, column.name)),
                    dialect.quoteIdentifier(
                        aliases::unjoinedRelationColumn(model->tableName, foreignModelFieldName, column.name)));
            }
        }
    }

    return selectFields;
}

auto DefaultSelectCommand::getJoins(bool shouldJoin, model::ModelView model, const SqlDialect& dialect) -> std::string
{
    if (not shouldJoin)
    {
        return {};
    }

    std::string joins;

    for (const auto& column : model->columns)
    {
        if (column.kind != model::FieldKind::ToOne)
        {
            continue;
        }

        const auto target = model.resolveTarget(column);
        if (target == nullptr)
        {
            throw std::logic_error{"To-one relation target is not available in the schema"};
        }
        std::vector<std::string> joinPredicates;

        for (const auto& targetColumn : target->columns)
        {
            if (targetColumn.isPrimaryKey)
            {
                joinPredicates.push_back(std::format(
                    "{} = {}", aliases::qualifiedIdentifier(dialect, column.name, targetColumn.name),
                    aliases::qualifiedIdentifier(dialect, model->tableName,
                                                 aliases::joinedRelationColumn(column.name, targetColumn.name))));
            }
        }

        joins += std::format(" LEFT JOIN {} AS {} ON {} ", dialect.quoteIdentifier(target->tableName),
                             dialect.quoteIdentifier(column.name), join(joinPredicates, " AND "));
    }

    if (not joins.empty())
    {
        joins.pop_back();
    }

    return joins;
}

auto DefaultSelectCommand::getGroupBy(const query::SelectSpec& spec, RenderContext& context) -> std::string
{
    if (spec.groupBy.empty())
    {
        return {};
    }

    std::vector<std::string> groupByClauses;
    groupByClauses.reserve(spec.groupBy.size());

    for (const auto& column : spec.groupBy)
    {
        groupByClauses.push_back(renderColumn(column, context));
    }

    return " GROUP BY " + join(groupByClauses, ", ");
}

auto DefaultSelectCommand::getHaving(const std::optional<query::AggregatePredicate>& having,
                                     RenderContext& context) -> std::string
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

auto DefaultSelectCommand::renderAggregatePredicate(const query::AggregatePredicateNode& node,
                                                    RenderContext& context) -> std::string
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

auto DefaultSelectCommand::getOrderBy(const query::SelectSpec& spec, RenderContext& context) -> std::string
{
    if (spec.orderBy.empty())
    {
        return {};
    }

    std::vector<std::string> orderByClauses;
    orderByClauses.reserve(spec.orderBy.size());

    for (const auto& orderBy : spec.orderBy)
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
