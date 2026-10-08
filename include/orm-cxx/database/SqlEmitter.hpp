#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "orm-cxx/database/CompiledSqlFlavor.hpp"
#include "orm-cxx/database/SqlDialect.hpp"
#include "orm-cxx/database/SqlNameValidation.hpp"
#include "orm-cxx/model/ModelView.hpp"
#include "orm-cxx/query/Aggregate.hpp"
#include "orm-cxx/query/OrderBy.hpp"

namespace orm::db
{
namespace detail
{
inline constexpr auto noSqlNode = std::numeric_limits<std::size_t>::max();
enum class SqlOperation
{
    Select,
    Update,
    Remove
};
enum class SqlNodeKind
{
    Comparison,
    Null,
    List,
    Between,
    And,
    Or,
    Not,
    Collection,
    Raw
};

struct SqlSource
{
    std::array<std::string_view, 2> pathParts{};
    std::size_t pathSize{};
    bool isAggregate{};
    query::detail::AggregateFunction function{};
};

struct SqlNode
{
    SqlNodeKind kind{};
    SqlSource source{};
    unsigned operation{};
    std::size_t left = noSqlNode;
    std::size_t right = noSqlNode;
    std::size_t firstParameter{};
    std::size_t parameterCount{};
    std::string_view relation{};
    std::string_view rawSql{};
};

struct SqlOrder
{
    SqlSource source{};
    query::detail::OrderDirection direction{};
    bool isRaw{};
    std::string_view rawSql{};
};
struct SqlProjection
{
    SqlSource source{};
    std::string_view alias{};
};
struct SqlAssignment
{
    SqlSource source{};
    std::size_t parameter{};
};
struct SqlBindingDescriptor
{
    model::ColumnType logicalType{};
    std::size_t index{};
    std::string_view rawName{};
};

struct SqlQueryView
{
    SqlOperation operation{};
    std::span<const SqlNode> nodes{};
    std::size_t predicate = noSqlNode;
    std::size_t having = noSqlNode;
    std::span<const SqlOrder> orders{};
    std::span<const SqlSource> groups{};
    std::span<const SqlProjection> projections{};
    std::span<const SqlAssignment> assignments{};
    std::span<const std::string_view> includes{};
    std::span<const SqlBindingDescriptor> bindings{};
    bool isDistinct{};
    bool shouldJoin = true;
    bool hasLimit{};
    bool hasOffset{};
    bool boundPagination{};
    std::size_t limitParameter{};
    std::size_t offsetParameter{};
    std::size_t literalLimit{};
    std::size_t literalOffset{};
};

// Temporary storage is also valid during C++20 constant evaluation. The
// compiled statement copies it into exactly sized arrays before returning.
struct SqlProgram
{
    SqlOperation operation{};
    std::vector<SqlNode> nodes;
    std::size_t predicate = noSqlNode;
    std::size_t having = noSqlNode;
    std::vector<SqlOrder> orders;
    std::vector<SqlSource> groups;
    std::vector<SqlProjection> projections;
    std::vector<SqlAssignment> assignments;
    std::vector<std::string_view> includes;
    std::vector<SqlBindingDescriptor> bindings;
    bool isDistinct{};
    bool shouldJoin = true;
    bool hasLimit{};
    bool hasOffset{};
    bool boundPagination{};
    std::size_t limitParameter{};
    std::size_t offsetParameter{};
    std::size_t literalLimit{};
    std::size_t literalOffset{};

    [[nodiscard]] constexpr auto view() const -> SqlQueryView
    {
        return {operation,       nodes,          predicate,       having,       orders,       groups,   projections,
                assignments,     includes,       bindings,        isDistinct,   shouldJoin,   hasLimit, hasOffset,
                boundPagination, limitParameter, offsetParameter, literalLimit, literalOffset};
    }
};

[[nodiscard]] constexpr auto decimal(std::size_t value) -> std::string
{
    std::string result;
    do
    {
        result.push_back(static_cast<char>('0' + value % 10));
        value /= 10;
    } while (value != 0);
    std::ranges::reverse(result);
    return result;
}
template <typename... Parts>
[[nodiscard]] constexpr auto sqlConcat(const Parts&... parts) -> std::string
{
    std::string result;
    (result.append(parts), ...);
    return result;
}
[[nodiscard]] constexpr auto parameterName(const SqlBindingDescriptor& descriptor) -> std::string
{
    return descriptor.rawName.empty() ? sqlConcat("orm_p", decimal(descriptor.index)) : std::string{descriptor.rawName};
}
[[nodiscard]] constexpr auto quoteStandardIdentifier(std::string_view identifier,
                                                     CompiledSqlFlavor flavor) -> std::string
{
    if (identifier.empty())
        throw std::invalid_argument{"SQL identifier cannot be empty"};
    if (identifier.find('\0') != std::string_view::npos)
        throw std::invalid_argument{flavor == CompiledSqlFlavor::PostgreSQL ?
                                        "PostgreSQL identifiers must not contain an embedded NUL byte" :
                                        "SQLite identifiers must not contain an embedded NUL byte"};
    if (flavor == CompiledSqlFlavor::PostgreSQL && identifier.size() > 63)
        throw std::invalid_argument{"PostgreSQL identifiers must not exceed 63 bytes"};
    std::string result{"\""};
    for (const auto character : identifier)
    {
        if (character == '"')
            result += '"';
        result += character;
    }
    result += '"';
    return result;
}

template <CompiledSqlFlavor Flavor>
struct StaticSqlPolicy
{
    [[nodiscard]] constexpr auto quoteIdentifier(std::string_view name) const -> std::string
    {
        return quoteStandardIdentifier(name, Flavor);
    }
    [[nodiscard]] constexpr auto bindMarker(std::string_view name) const -> std::string
    {
        if (!isPortableBindName(name))
            throw std::invalid_argument{"Invalid SQL bind parameter name"};
        return sqlConcat(":", name);
    }
    [[nodiscard]] constexpr auto renderAggregateResult(std::string_view value, bool exact) const -> std::string
    {
        if constexpr (Flavor == CompiledSqlFlavor::PostgreSQL)
            if (exact)
                return sqlConcat("CAST(", value, " AS TEXT)");
        return std::string{value};
    }
    [[nodiscard]] constexpr auto pagination(const SqlQueryView& query) const -> std::string
    {
        std::string result;
        if (query.hasLimit)
        {
            result += " LIMIT ";
            result += query.boundPagination ? bindMarker(parameterName(query.bindings[query.limitParameter])) :
                                              decimal(query.literalLimit);
        }
        else if constexpr (Flavor == CompiledSqlFlavor::SQLite)
            if (query.hasOffset)
                result += " LIMIT -1";
        if (query.hasOffset)
        {
            result += " OFFSET ";
            result += query.boundPagination ? bindMarker(parameterName(query.bindings[query.offsetParameter])) :
                                              decimal(query.literalOffset);
        }
        return result;
    }
};

struct RuntimeSqlPolicy
{
    const SqlDialect& dialect;
    [[nodiscard]] auto quoteIdentifier(std::string_view name) const -> std::string
    {
        return dialect.quoteIdentifier(name);
    }
    [[nodiscard]] auto bindMarker(std::string_view name) const -> std::string
    {
        return dialect.bindMarker(name);
    }
    [[nodiscard]] auto renderAggregateResult(std::string_view value, bool exact) const -> std::string
    {
        return dialect.renderAggregateResult(value, exact);
    }
    [[nodiscard]] auto pagination(const SqlQueryView& query) const -> std::string
    {
        if (query.boundPagination)
            throw std::logic_error{"A runtime dialect requires literal pagination"};
        return dialect.renderPagination(
            PaginationSpec{.limit = query.hasLimit ? std::optional{query.literalLimit} : std::nullopt,
                           .offset = query.hasOffset ? std::optional{query.literalOffset} : std::nullopt,
                           .hasOrderBy = !query.orders.empty()});
    }
};

[[nodiscard]] constexpr auto sqlPath(const SqlSource& source) -> std::string
{
    if (source.pathSize == 0)
        return {};
    return source.pathSize == 1 ? std::string{source.pathParts[0]} :
                                  sqlConcat(source.pathParts[0], ".", source.pathParts[1]);
}
[[nodiscard]] constexpr auto parseSqlSource(std::string_view path) -> SqlSource
{
    SqlSource result;
    std::size_t start = 0;
    while (start <= path.size())
    {
        const auto dot = path.find('.', start);
        const auto part = path.substr(start, dot - start);
        if (part.empty())
            throw std::invalid_argument{sqlConcat("Column path contains an empty segment: ", path)};
        if (result.pathSize == result.pathParts.size())
            throw std::invalid_argument{sqlConcat("Only one level of related model paths is supported: ", path)};
        result.pathParts[result.pathSize++] = part;
        if (dot == std::string_view::npos)
            break;
        start = dot + 1;
    }
    return result;
}
[[nodiscard]] constexpr auto requireSqlColumn(model::ModelView model, std::string_view name) -> const model::ColumnView&
{
    const auto* column = model.findColumn(name);
    if (column == nullptr)
        throw std::invalid_argument{sqlConcat("Unknown column path segment: ", name)};
    return *column;
}
[[nodiscard]] constexpr auto requireSqlTarget(model::ModelView model,
                                              const model::ColumnView& column) -> model::ModelView
{
    if (column.kind != model::FieldKind::ToOne)
        throw std::invalid_argument{sqlConcat("Column is not a related model: ", column.fieldName)};
    const auto target = model.resolveTarget(column);
    if (!target)
        throw std::logic_error{"To-one relation target is not available in the schema"};
    return target;
}
[[nodiscard]] constexpr auto sqlPrimaryKey(model::ModelView model) -> std::vector<const model::ColumnView*>
{
    std::vector<const model::ColumnView*> result;
    for (const auto& column : model->columns)
        if (column.isPrimaryKey)
        {
            if (column.kind == model::FieldKind::ToOne)
                throw std::invalid_argument{"Relations with model-valued primary-key fields are not supported"};
            result.push_back(&column);
        }
    if (result.empty())
        throw std::invalid_argument{"Collection relation endpoint must define a primary key"};
    return result;
}
struct SqlColumnInfo
{
    const model::ColumnView* column{};
    const model::ColumnView* relation{};
    model::ColumnType logicalType{};
    bool isNotNull{};
};
[[nodiscard]] constexpr auto resolveSqlColumn(const SqlSource& source, model::ModelView model,
                                              bool write = false) -> SqlColumnInfo
{
    if (source.pathSize == 1)
    {
        const auto& column = requireSqlColumn(model, source.pathParts[0]);
        if (column.kind == model::FieldKind::ToOne)
            throw std::invalid_argument{sqlConcat(write ?
                                                      "Use a related primary-key field path in write queries: " :
                                                      "Use a related field path instead of the related model itself: ",
                                                  sqlPath(source))};
        return {&column, nullptr, column.type.value(), column.isNotNull};
    }
    if (source.pathSize == 2)
    {
        const auto& relation = requireSqlColumn(model, source.pathParts[0]);
        const auto target = requireSqlTarget(model, relation);
        const auto& column = requireSqlColumn(target, source.pathParts[1]);
        if (write && !column.isPrimaryKey)
            throw std::invalid_argument{
                sqlConcat("Only related primary-key fields can be used in write queries: ", sqlPath(source))};
        return {&column, &relation, column.type.value(), relation.isNotNull};
    }
    throw std::invalid_argument{sqlConcat("Only one level of related model paths is supported: ", sqlPath(source))};
}

struct SqlRenderScope
{
    model::ModelView model;
    bool shouldJoin = true;
    bool write{};
    bool allowCollections = true;
    std::string tableAlias;
    std::vector<std::pair<std::string, std::string>> relationAliases;
};
template <typename Policy>
[[nodiscard]] constexpr auto sqlQualified(const Policy& policy, std::string_view qualifier,
                                          std::string_view name) -> std::string
{
    return sqlConcat(policy.quoteIdentifier(qualifier), ".", policy.quoteIdentifier(name));
}
template <typename Policy>
[[nodiscard]] constexpr auto emitSqlColumn(const SqlSource& source, const SqlRenderScope& scope, const Policy& policy,
                                           bool qualify = true) -> std::string
{
    const auto info = resolveSqlColumn(source, scope.model, scope.write);
    const auto root = scope.tableAlias.empty() ? scope.model->tableName : std::string_view{scope.tableAlias};
    if (info.relation == nullptr)
        return qualify ? sqlQualified(policy, root, info.column->name) : policy.quoteIdentifier(info.column->name);
    if (scope.write || !scope.shouldJoin)
    {
        if (!info.column->isPrimaryKey)
            throw std::invalid_argument{
                sqlConcat("Cannot filter by non-id related field when joining is disabled: ", sqlPath(source))};
        const auto name = sqlConcat(info.relation->name, "_", info.column->name);
        return qualify ? sqlQualified(policy, root, name) : policy.quoteIdentifier(name);
    }
    auto qualifier = info.relation->name;
    for (const auto& entry : scope.relationAliases)
        if (entry.first == qualifier)
        {
            qualifier = entry.second;
            break;
        }
    return sqlQualified(policy, qualifier, info.column->name);
}
[[nodiscard]] constexpr auto sqlComparison(unsigned operation) -> std::string_view
{
    using enum query::detail::ComparisonOperator;
    switch (static_cast<query::detail::ComparisonOperator>(operation))
    {
    case Equal:
        return "=";
    case NotEqual:
        return "!=";
    case Greater:
        return ">";
    case GreaterOrEqual:
        return ">=";
    case Less:
        return "<";
    case LessOrEqual:
        return "<=";
    case Like:
        return "LIKE";
    case NotLike:
        return "NOT LIKE";
    }
    throw std::invalid_argument{"Unsupported comparison operator"};
}
[[nodiscard]] constexpr auto sqlAggregateName(query::detail::AggregateFunction function) -> std::string_view
{
    using enum query::detail::AggregateFunction;
    switch (function)
    {
    case Count:
    case CountAll:
        return "COUNT";
    case Sum:
        return "SUM";
    case Avg:
        return "AVG";
    case Min:
        return "MIN";
    case Max:
        return "MAX";
    }
    throw std::invalid_argument{"Unsupported aggregate function"};
}
template <typename Policy>
[[nodiscard]] constexpr auto emitSqlSource(const SqlSource& source, const SqlRenderScope& scope, const Policy& policy,
                                           bool projection = false) -> std::string
{
    if (!source.isAggregate)
        return emitSqlColumn(source, scope, policy);
    auto result = sqlConcat(sqlAggregateName(source.function), "(");
    result +=
        source.function == query::detail::AggregateFunction::CountAll ? "*" : emitSqlColumn(source, scope, policy);
    result += ')';
    return projection ?
               policy.renderAggregateResult(result, source.function == query::detail::AggregateFunction::Sum ||
                                                        source.function == query::detail::AggregateFunction::Avg) :
               result;
}
[[nodiscard]] constexpr auto hasSqlAlias(const std::vector<std::string>& names, std::string_view name) -> bool
{
    return std::ranges::find(names, name) != names.end();
}
[[nodiscard]] constexpr auto uniqueSqlAlias(std::string_view base, const std::vector<std::string>& names) -> std::string
{
    std::string result{base};
    std::size_t suffix = 1;
    while (hasSqlAlias(names, result))
        result = sqlConcat(base, "_", decimal(suffix++));
    return result;
}
template <typename Policy>
[[nodiscard]] constexpr auto emitSqlJoins(const SqlRenderScope& scope, const Policy& policy) -> std::string
{
    std::string result;
    for (const auto& relation : scope.model->columns)
    {
        if (relation.kind != model::FieldKind::ToOne)
            continue;
        const auto target = requireSqlTarget(scope.model, relation);
        auto alias = std::string{relation.name};
        for (const auto& entry : scope.relationAliases)
            if (entry.first == relation.name)
            {
                alias = entry.second;
                break;
            }
        result += sqlConcat(" LEFT JOIN ", policy.quoteIdentifier(target->tableName), " AS ",
                            policy.quoteIdentifier(alias), " ON ");
        bool first = true;
        for (const auto& column : target->columns)
            if (column.isPrimaryKey)
            {
                if (!first)
                    result += " AND ";
                first = false;
                const auto root =
                    scope.tableAlias.empty() ? scope.model->tableName : std::string_view{scope.tableAlias};
                result += sqlConcat(sqlQualified(policy, alias, column.name), " = ",
                                    sqlQualified(policy, root, sqlConcat(relation.name, "_", column.name)));
            }
        if (scope.allowCollections)
            result += ' ';
    }
    if (scope.allowCollections && !result.empty())
        result.pop_back();
    return result;
}

template <typename Policy>
[[nodiscard]] constexpr auto emitSqlPredicate(const SqlQueryView& query, std::size_t index, const SqlRenderScope& scope,
                                              const Policy& policy) -> std::string;
template <typename Policy>
[[nodiscard]] constexpr auto emitSqlCollection(const SqlQueryView& query, const SqlNode& node,
                                               const SqlRenderScope& scope, const Policy& policy) -> std::string
{
    if (!scope.allowCollections)
        throw std::invalid_argument{"Nested collection predicates are not supported"};
    const auto* relation = scope.model.findRelation(node.relation);
    if (relation == nullptr || relation->kind == model::RelationKind::ToOne)
        throw std::invalid_argument{sqlConcat("Unknown collection relation: ", node.relation)};
    if (node.operation != static_cast<unsigned>(query::detail::CollectionOperator::Exists) && node.left == noSqlNode)
        throw std::invalid_argument{"Collection any/none requires an element predicate"};
    const auto ownerKeys = sqlPrimaryKey(scope.model);
    const auto target = scope.model.resolveTarget(*relation);
    if (!target)
        throw std::logic_error{"Collection relation target is not available in the schema"};
    const auto targetKeys = sqlPrimaryKey(target);
    const auto outer = scope.tableAlias.empty() ? std::string{scope.model->tableName} : scope.tableAlias;
    std::vector<std::string> reserved{outer};
    for (const auto& column : target->columns)
        if (column.kind == model::FieldKind::ToOne)
            reserved.emplace_back(column.name);
    const auto targetAlias = uniqueSqlAlias("orm_relation_target", reserved);
    reserved.push_back(targetAlias);
    const auto junctionAlias = uniqueSqlAlias("orm_relation_junction", reserved);
    reserved.push_back(junctionAlias);
    std::vector<std::string> assigned{outer, targetAlias, junctionAlias};
    SqlRenderScope nested{target, true, false, false, targetAlias, {}};
    for (const auto& column : target->columns)
        if (column.kind == model::FieldKind::ToOne)
        {
            const auto alias =
                hasSqlAlias(assigned, column.name) ? uniqueSqlAlias(column.name, reserved) : std::string{column.name};
            nested.relationAliases.emplace_back(column.name, alias);
            assigned.push_back(alias);
            reserved.push_back(alias);
        }
    std::string from;
    std::string predicate;
    if (relation->kind == model::RelationKind::OneToMany)
    {
        const auto* mapped = target.findRelation(relation->mappedBy);
        if (mapped == nullptr || mapped->kind != model::RelationKind::ToOne)
            throw std::invalid_argument{sqlConcat("Invalid OneToMany mappedBy relation: ", relation->fieldName)};
        for (const auto* key : ownerKeys)
        {
            if (!predicate.empty())
                predicate += " AND ";
            predicate += sqlConcat(sqlQualified(policy, targetAlias, sqlConcat(mapped->columnName, "_", key->name)),
                                   " = ", sqlQualified(policy, outer, key->name));
        }
        from = sqlConcat(policy.quoteIdentifier(target->tableName), " AS ", policy.quoteIdentifier(targetAlias),
                         emitSqlJoins(nested, policy));
    }
    else
    {
        const auto junction = scope.model.resolveJunction(*relation);
        if (!junction.isConfigured())
            throw std::invalid_argument{
                sqlConcat("ManyToMany relation has no junction mapping: ", relation->fieldName)};
        if (junction.ownerColumns.size() != ownerKeys.size() || junction.targetColumns.size() != targetKeys.size())
            throw std::invalid_argument{
                sqlConcat("Junction columns do not match relation endpoint keys: ", junction.tableName)};
        for (std::size_t i = 0; i < ownerKeys.size(); ++i)
        {
            if (!predicate.empty())
                predicate += " AND ";
            predicate += sqlConcat(sqlQualified(policy, junctionAlias, junction.ownerColumns[i]), " = ",
                                   sqlQualified(policy, outer, ownerKeys[i]->name));
        }
        std::string targetJoin;
        for (std::size_t i = 0; i < targetKeys.size(); ++i)
        {
            if (!targetJoin.empty())
                targetJoin += " AND ";
            targetJoin += sqlConcat(sqlQualified(policy, targetAlias, targetKeys[i]->name), " = ",
                                    sqlQualified(policy, junctionAlias, junction.targetColumns[i]));
        }
        from = sqlConcat(policy.quoteIdentifier(junction.tableName), " AS ", policy.quoteIdentifier(junctionAlias),
                         " JOIN ", policy.quoteIdentifier(target->tableName), " AS ",
                         policy.quoteIdentifier(targetAlias), " ON ", targetJoin, emitSqlJoins(nested, policy));
    }
    if (node.left != noSqlNode)
        predicate += sqlConcat(" AND ", emitSqlPredicate(query, node.left, nested, policy));
    const auto exists = sqlConcat("EXISTS (SELECT 1 FROM ", from, " WHERE ", predicate, ")");
    return node.operation == static_cast<unsigned>(query::detail::CollectionOperator::None) ?
               sqlConcat("NOT (", exists, ")") :
               exists;
}
template <typename Policy>
[[nodiscard]] constexpr auto emitSqlPredicate(const SqlQueryView& query, std::size_t index, const SqlRenderScope& scope,
                                              const Policy& policy) -> std::string
{
    const auto& node = query.nodes[index];
    auto bind = [&](std::size_t offset)
    { return policy.bindMarker(parameterName(query.bindings[node.firstParameter + offset])); };
    switch (node.kind)
    {
    case SqlNodeKind::Comparison:
        if (node.source.isAggregate && node.operation >= static_cast<unsigned>(query::detail::ComparisonOperator::Like))
            throw std::invalid_argument{"Unsupported aggregate comparison operator"};
        return sqlConcat(emitSqlSource(node.source, scope, policy), " ", sqlComparison(node.operation), " ", bind(0));
    case SqlNodeKind::Null:
        return sqlConcat(emitSqlColumn(node.source, scope, policy),
                         node.operation == static_cast<unsigned>(query::detail::NullOperator::IsNull) ? " IS NULL" :
                                                                                                        " IS NOT NULL");
    case SqlNodeKind::List:
    {
        const auto in = node.operation == static_cast<unsigned>(query::detail::ListOperator::In);
        if (node.parameterCount == 0)
            return std::string{in ? "(1 = 0)" : "(1 = 1)"};
        auto result = sqlConcat(emitSqlColumn(node.source, scope, policy), in ? " IN (" : " NOT IN (");
        for (std::size_t i = 0; i < node.parameterCount; ++i)
        {
            if (i != 0)
                result += ", ";
            result += bind(i);
        }
        result += ')';
        return result;
    }
    case SqlNodeKind::Between:
        return sqlConcat(emitSqlColumn(node.source, scope, policy),
                         node.operation == static_cast<unsigned>(query::detail::BetweenOperator::Between) ?
                             " BETWEEN " :
                             " NOT BETWEEN ",
                         bind(0), " AND ", bind(1));
    case SqlNodeKind::And:
    case SqlNodeKind::Or:
        return sqlConcat("(", emitSqlPredicate(query, node.left, scope, policy),
                         node.kind == SqlNodeKind::And ? " AND " : " OR ",
                         emitSqlPredicate(query, node.right, scope, policy), ")");
    case SqlNodeKind::Not:
        return sqlConcat("(NOT (", emitSqlPredicate(query, node.left, scope, policy), "))");
    case SqlNodeKind::Raw:
        return std::string{node.rawSql};
    case SqlNodeKind::Collection:
        return emitSqlCollection(query, node, scope, policy);
    }
    throw std::logic_error{"Unknown SQL predicate node"};
}
template <typename Policy>
[[nodiscard]] constexpr auto emitSqlFullFields(const SqlRenderScope& scope, const Policy& policy) -> std::string
{
    std::string result;
    auto append = [&](std::string expression, std::string alias)
    {
        if (!result.empty())
            result += ", ";
        result += sqlConcat(expression, " AS ", policy.quoteIdentifier(alias));
    };
    for (const auto& column : scope.model->columns)
    {
        if (column.kind == model::FieldKind::Scalar)
            append(sqlQualified(policy, scope.model->tableName, column.name),
                   sqlConcat(scope.model->tableName, "_", column.name));
        else
        {
            const auto target = requireSqlTarget(scope.model, column);
            for (const auto& child : target->columns)
                if (scope.shouldJoin)
                    append(sqlQualified(policy, column.name, child.name), sqlConcat(column.name, "_", child.name));
                else if (child.isPrimaryKey)
                    append(sqlQualified(policy, scope.model->tableName, sqlConcat(column.name, "_", child.name)),
                           sqlConcat(scope.model->tableName, "_", column.name, "_", child.name));
        }
    }
    return result;
}
template <typename Policy>
[[nodiscard]] constexpr auto emitSql(const SqlQueryView& query, model::ModelView model,
                                     const Policy& policy) -> std::string
{
    SqlRenderScope scope{model, query.shouldJoin, query.operation != SqlOperation::Select, true, {}, {}};
    std::string result;
    if (query.operation == SqlOperation::Select)
    {
        result = query.isDistinct ? "SELECT DISTINCT " : "SELECT ";
        if (query.projections.empty())
            result += emitSqlFullFields(scope, policy);
        else
            for (std::size_t i = 0; i < query.projections.size(); ++i)
            {
                if (i != 0)
                    result += ", ";
                result += sqlConcat(emitSqlSource(query.projections[i].source, scope, policy, true), " AS ",
                                    policy.quoteIdentifier(query.projections[i].alias));
            }
        result += sqlConcat(" FROM ", policy.quoteIdentifier(model->tableName));
        if (query.shouldJoin)
            result += emitSqlJoins(scope, policy);
    }
    else if (query.operation == SqlOperation::Update)
    {
        if (query.assignments.empty())
            throw std::invalid_argument{"UPDATE requires at least one assignment"};
        if (query.predicate == noSqlNode)
            throw std::invalid_argument{"UPDATE requires a WHERE predicate"};
        result = sqlConcat("UPDATE ", policy.quoteIdentifier(model->tableName), " SET ");
        for (std::size_t i = 0; i < query.assignments.size(); ++i)
        {
            if (i != 0)
                result += ", ";
            const auto& assignment = query.assignments[i];
            result += sqlConcat(emitSqlColumn(assignment.source, scope, policy, false), " = ",
                                policy.bindMarker(parameterName(query.bindings[assignment.parameter])));
        }
    }
    else
        result = sqlConcat("DELETE FROM ", policy.quoteIdentifier(model->tableName));
    if (query.predicate != noSqlNode)
        result += sqlConcat(" WHERE ", emitSqlPredicate(query, query.predicate, scope, policy));
    if (!query.groups.empty())
    {
        result += " GROUP BY ";
        for (std::size_t i = 0; i < query.groups.size(); ++i)
        {
            if (i != 0)
                result += ", ";
            result += emitSqlColumn(query.groups[i], scope, policy);
        }
    }
    if (query.having != noSqlNode)
        result += sqlConcat(" HAVING ", emitSqlPredicate(query, query.having, scope, policy));
    if (!query.orders.empty())
    {
        result += " ORDER BY ";
        for (std::size_t i = 0; i < query.orders.size(); ++i)
        {
            if (i != 0)
                result += ", ";
            const auto& order = query.orders[i];
            result += order.isRaw ? std::string{order.rawSql} :
                                    sqlConcat(emitSqlColumn(order.source, scope, policy),
                                              order.direction == query::detail::OrderDirection::Asc ? " ASC" : " DESC");
        }
    }
    result += policy.pagination(query);
    result += ';';
    return result;
}
} // namespace detail
} // namespace orm::db
