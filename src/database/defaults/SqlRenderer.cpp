#include "SqlRenderer.hpp"

#include <stdexcept>
#include <string_view>
#include <variant>

#include "orm-cxx/database/SqlEmitter.hpp"
#include "orm-cxx/query/UpdateSpec.hpp"

namespace orm::db::commands
{
namespace
{
template <class... Ts>
struct Overloaded : Ts...
{
    using Ts::operator()...;
};
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

using detail::SqlNode;
using detail::SqlNodeKind;
using detail::SqlProgram;
using detail::SqlSource;

auto source(const query::detail::Column& column) -> SqlSource
{
    return detail::parseSqlSource(column.getPath());
}
auto source(const query::detail::AggregateExpression& expression) -> SqlSource
{
    if (expression.function != query::detail::AggregateFunction::CountAll && !expression.column.has_value())
        throw std::invalid_argument{"Aggregate function requires a source column"};
    auto result = expression.column.has_value() ? source(*expression.column) : SqlSource{};
    result.isAggregate = true;
    result.function = expression.function;
    return result;
}
auto addValue(SqlProgram& program, RenderContext& context, const query::QueryValue& value) -> std::size_t
{
    (void)addAutomaticParameter(context, value);
    const auto index = program.bindings.size();
    program.bindings.push_back({value.getLogicalType(), context.nextParameterIndex - 1, {}});
    return index;
}
auto addNull(SqlProgram& program, RenderContext& context, model::ColumnType type) -> std::size_t
{
    (void)addNullParameter(context, type);
    const auto index = program.bindings.size();
    program.bindings.push_back({type, context.nextParameterIndex - 1, {}});
    return index;
}
auto appendPredicate(SqlProgram& program, RenderContext& context,
                     const query::detail::PredicateNode& predicate) -> std::size_t
{
    auto node = std::visit(
        Overloaded{[&](const query::detail::ComparisonExpression& expression)
                   {
                       return SqlNode{.kind = SqlNodeKind::Comparison,
                                      .source = source(expression.column),
                                      .operation = static_cast<unsigned>(expression.comparisonOperator),
                                      .firstParameter = addValue(program, context, expression.value),
                                      .parameterCount = 1};
                   },
                   [&](const query::detail::NullExpression& expression)
                   {
                       return SqlNode{.kind = SqlNodeKind::Null,
                                      .source = source(expression.column),
                                      .operation = static_cast<unsigned>(expression.nullOperator)};
                   },
                   [&](const query::detail::ListExpression& expression)
                   {
                       SqlNode result{.kind = SqlNodeKind::List,
                                      .source = source(expression.column),
                                      .operation = static_cast<unsigned>(expression.listOperator),
                                      .firstParameter = program.bindings.size(),
                                      .parameterCount = expression.values.size()};
                       for (const auto& value : expression.values)
                           (void)addValue(program, context, value);
                       return result;
                   },
                   [&](const query::detail::BetweenExpression& expression)
                   {
                       SqlNode result{.kind = SqlNodeKind::Between,
                                      .source = source(expression.column),
                                      .operation = static_cast<unsigned>(expression.betweenOperator),
                                      .firstParameter = addValue(program, context, expression.lowerValue),
                                      .parameterCount = 2};
                       (void)addValue(program, context, expression.upperValue);
                       return result;
                   },
                   [&](const query::detail::LogicalExpression& expression)
                   {
                       const auto left = appendPredicate(program, context, *expression.left);
                       const auto right = appendPredicate(program, context, *expression.right);
                       return SqlNode{.kind = expression.logicalOperator == query::detail::LogicalOperator::And ?
                                                  SqlNodeKind::And :
                                                  SqlNodeKind::Or,
                                      .left = left,
                                      .right = right};
                   },
                   [&](const query::detail::NotExpression& expression) {
                       return SqlNode{.kind = SqlNodeKind::Not,
                                      .left = appendPredicate(program, context, *expression.predicate)};
                   },
                   [&](const query::detail::RawExpression& expression)
                   {
                       for (const auto& parameter : expression.parameters)
                       {
                           if (parameter.name.empty() || parameter.name.front() == ':')
                               throw std::invalid_argument{"Raw query parameter name must not be empty or include ':'"};
                           if (parameter.name.starts_with("orm_p"))
                               throw std::invalid_argument{"Raw query parameter cannot use reserved prefix orm_p: " +
                                                           parameter.name};
                           (void)context.dialect.bindMarker(parameter.name);
                           if (!context.parameterNames.insert(parameter.name).second)
                               throw std::invalid_argument{"Duplicate query parameter: " + parameter.name};
                           context.parameters.push_back(
                               {.name = parameter.name, .value = parameter.value, .nullType = std::nullopt});
                           program.bindings.push_back({parameter.value.getLogicalType(), 0, parameter.name});
                       }
                       return SqlNode{.kind = SqlNodeKind::Raw, .rawSql = expression.sql};
                   },
                   [&](const query::detail::CollectionExpression& expression)
                   {
                       return SqlNode{.kind = SqlNodeKind::Collection,
                                      .operation = static_cast<unsigned>(expression.collectionOperator),
                                      .left = expression.predicate == nullptr ?
                                                  detail::noSqlNode :
                                                  appendPredicate(program, context, *expression.predicate),
                                      .relation = expression.relation};
                   }},
        predicate.expression);
    const auto index = program.nodes.size();
    program.nodes.push_back(node);
    return index;
}
auto appendHaving(SqlProgram& program, RenderContext& context,
                  const query::detail::AggregatePredicateNode& predicate) -> std::size_t
{
    auto node = std::visit(
        Overloaded{[&](const query::detail::AggregateComparisonExpression& expression)
                   {
                       return SqlNode{.kind = SqlNodeKind::Comparison,
                                      .source = source(expression.aggregate),
                                      .operation = static_cast<unsigned>(expression.comparisonOperator),
                                      .firstParameter = addValue(program, context, expression.value),
                                      .parameterCount = 1};
                   },
                   [&](const query::detail::AggregateLogicalExpression& expression)
                   {
                       const auto left = appendHaving(program, context, *expression.left);
                       const auto right = appendHaving(program, context, *expression.right);
                       return SqlNode{.kind = expression.logicalOperator == query::detail::LogicalOperator::And ?
                                                  SqlNodeKind::And :
                                                  SqlNodeKind::Or,
                                      .left = left,
                                      .right = right};
                   },
                   [&](const query::detail::AggregateNotExpression& expression) {
                       return SqlNode{.kind = SqlNodeKind::Not,
                                      .left = appendHaving(program, context, *expression.predicate)};
                   }},
        predicate.expression);
    const auto index = program.nodes.size();
    program.nodes.push_back(node);
    return index;
}
auto scope(const RenderContext& context) -> detail::SqlRenderScope
{
    detail::SqlRenderScope result{context.model,
                                  context.shouldJoin,
                                  context.columnRenderMode == ColumnRenderMode::WritePredicate,
                                  context.allowCollectionPredicates,
                                  context.tableAlias,
                                  {}};
    for (const auto& pair : context.relationAliases)
        result.relationAliases.push_back(pair);
    return result;
}
} // namespace

auto renderColumn(const query::detail::Column& column, const RenderContext& context) -> std::string
{
    return detail::emitSqlColumn(source(column), scope(context), detail::RuntimeSqlPolicy{context.dialect});
}
auto renderWriteColumn(const query::detail::Column& column, model::ModelView modelView, const SqlDialect& dialect,
                       bool qualifyWithTable) -> WriteColumn
{
    const auto columnSource = source(column);
    const auto info = detail::resolveSqlColumn(columnSource, modelView, true);
    const detail::SqlRenderScope writeScope{modelView, false, true, true, {}, {}};
    return {.sql = detail::emitSqlColumn(columnSource, writeScope, detail::RuntimeSqlPolicy{dialect}, qualifyWithTable),
            .type = info.logicalType,
            .isNotNull = info.isNotNull};
}
auto renderWhere(const std::optional<query::detail::Predicate>& predicate, RenderContext& context) -> std::string
{
    return predicate.has_value() ? renderWhere(*predicate, context) : std::string{};
}
auto renderWhere(const query::detail::Predicate& predicate, RenderContext& context) -> std::string
{
    SqlProgram program;
    program.predicate = appendPredicate(program, context, predicate.getNode());
    return " WHERE " + detail::emitSqlPredicate(program.view(), program.predicate, scope(context),
                                                detail::RuntimeSqlPolicy{context.dialect});
}
auto addAutomaticParameter(RenderContext& context, const query::QueryValue& value) -> std::string
{
    std::string name;
    do
    {
        name = "orm_p" + std::to_string(context.nextParameterIndex++);
    } while (context.parameterNames.contains(name));
    context.parameterNames.insert(name);
    context.parameters.push_back({.name = name, .value = value, .nullType = std::nullopt});
    return context.dialect.bindMarker(name);
}
auto addNullParameter(RenderContext& context, model::ColumnType type) -> std::string
{
    std::string name;
    do
    {
        name = "orm_p" + std::to_string(context.nextParameterIndex++);
    } while (context.parameterNames.contains(name));
    context.parameterNames.insert(name);
    context.parameters.push_back({.name = name, .value = std::nullopt, .nullType = type});
    return context.dialect.bindMarker(name);
}
auto renderSelectStatement(model::ModelView model, const query::detail::SelectSpec& spec,
                           const SqlDialect& dialect) -> SelectStatement
{
    RenderContext context{.model = model, .dialect = dialect, .shouldJoin = spec.shouldJoin};
    SqlProgram program;
    program.isDistinct = spec.isDistinct;
    program.shouldJoin = spec.shouldJoin;
    for (const auto& projection : spec.projections)
        program.projections.push_back(
            {std::visit([](const auto& item) { return source(item); }, projection.source), projection.resultField});
    if (spec.predicate.has_value())
        program.predicate = appendPredicate(program, context, spec.predicate->getNode());
    for (const auto& group : spec.groupBy)
        program.groups.push_back(source(group));
    if (spec.having.has_value())
        program.having = appendHaving(program, context, spec.having->getNode());
    for (const auto& order : spec.orderBy)
        program.orders.push_back(
            {order.isRaw ? SqlSource{} : source(order.column), order.direction, order.isRaw, order.rawSql});
    program.hasLimit = spec.limit.has_value();
    program.hasOffset = spec.offset.has_value();
    program.literalLimit = spec.limit.value_or(0);
    program.literalOffset = spec.offset.value_or(0);
    const auto sql = detail::emitSql(program.view(), model, detail::RuntimeSqlPolicy{dialect});
    return {.sql = sql, .parameters = std::move(context.parameters)};
}
auto renderUpdateStatement(model::ModelView model, const query::detail::UpdateSpec& spec,
                           const SqlDialect& dialect) -> Statement
{
    RenderContext context{
        .model = model, .dialect = dialect, .shouldJoin = false, .columnRenderMode = ColumnRenderMode::WritePredicate};
    SqlProgram program;
    program.operation = detail::SqlOperation::Update;
    program.shouldJoin = false;
    if (spec.assignments.empty())
        throw std::invalid_argument{"UPDATE requires at least one assignment"};
    if (!spec.predicate.has_value())
        throw std::invalid_argument{"UPDATE requires a WHERE predicate"};
    for (const auto& assignment : spec.assignments)
    {
        const auto columnSource = source(assignment.column);
        const auto info = detail::resolveSqlColumn(columnSource, model, true);
        if (!assignment.value.value.has_value() && info.isNotNull)
            throw std::invalid_argument{"Cannot assign NULL to NOT NULL column: " + assignment.column.getPath()};
        const auto parameter = assignment.value.value.has_value() ?
                                   addValue(program, context, *assignment.value.value) :
                                   addNull(program, context, info.logicalType);
        program.assignments.push_back({columnSource, parameter});
    }
    program.predicate = appendPredicate(program, context, spec.predicate->getNode());
    const auto sql = detail::emitSql(program.view(), model, detail::RuntimeSqlPolicy{dialect});
    return {.sql = sql, .parameters = std::move(context.parameters)};
}
auto renderRemoveStatement(model::ModelView model, const query::detail::Predicate& predicate,
                           const SqlDialect& dialect) -> Statement
{
    RenderContext context{
        .model = model, .dialect = dialect, .shouldJoin = false, .columnRenderMode = ColumnRenderMode::WritePredicate};
    SqlProgram program;
    program.operation = detail::SqlOperation::Remove;
    program.shouldJoin = false;
    program.predicate = appendPredicate(program, context, predicate.getNode());
    const auto sql = detail::emitSql(program.view(), model, detail::RuntimeSqlPolicy{dialect});
    return {.sql = sql, .parameters = std::move(context.parameters)};
}
} // namespace orm::db::commands
