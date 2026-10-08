#pragma once

#include <array>
#include <tuple>
#include <type_traits>
#include <utility>

#include "orm-cxx/database/SqlEmitter.hpp"
#include "orm-cxx/database/Statement.hpp"
#include "orm-cxx/model/Schema.hpp"
#include "orm-cxx/query/StaticPlan.hpp"

namespace orm::query::detail
{
namespace compiled
{
using namespace orm::db::detail;

template <typename Tuple, typename Callback>
constexpr auto visitTypes(Callback&& callback) -> void
{
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        (callback.template operator()<std::tuple_element_t<I, Tuple>>(), ...);
    }(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
}
template <typename E>
consteval auto source() -> SqlSource
{
    SqlSource result;
    if constexpr (E::kind == ExprKind::Aggregate)
    {
        if constexpr (!std::same_as<typename E::Source, void>)
            result = source<typename E::Source>();
        result.isAggregate = true;
        result.function = E::function;
    }
    else
    {
        result.pathSize = E::pathParts.size();
        for (std::size_t i = 0; i < result.pathSize; ++i)
            result.pathParts[i] = E::pathParts[i];
    }
    return result;
}
template <typename Value>
constexpr auto addBinding(SqlProgram& program) -> std::size_t
{
    const auto index = program.bindings.size();
    program.bindings.push_back({model::LogicalTypeTraits<Value>::value, index, {}});
    return index;
}
template <typename E>
constexpr auto appendExpression(SqlProgram& program) -> std::size_t
{
    SqlNode node;
    if constexpr (E::kind == ExprKind::Comparison || E::kind == ExprKind::Null || E::kind == ExprKind::List ||
                  E::kind == ExprKind::Between)
    {
        node.source = source<typename E::Source>();
        node.operation = static_cast<unsigned>(E::operation);
        node.firstParameter = program.bindings.size();
        if constexpr (E::kind == ExprKind::Comparison)
        {
            node.kind = SqlNodeKind::Comparison;
            node.parameterCount = 1;
        }
        else if constexpr (E::kind == ExprKind::Null)
            node.kind = SqlNodeKind::Null;
        else if constexpr (E::kind == ExprKind::Between)
        {
            node.kind = SqlNodeKind::Between;
            node.parameterCount = 2;
        }
        else
        {
            node.kind = SqlNodeKind::List;
            node.parameterCount = E::arity;
        }
        if constexpr (E::kind != ExprKind::Null)
            for (std::size_t i = 0; i < node.parameterCount; ++i)
                (void)addBinding<typename E::Source::Value>(program);
    }
    else if constexpr (E::kind == ExprKind::Logical)
    {
        node.kind = E::operation == LogicalOperator::And ? SqlNodeKind::And : SqlNodeKind::Or;
        node.left = appendExpression<typename E::Left>(program);
        node.right = appendExpression<typename E::Right>(program);
    }
    else if constexpr (E::kind == ExprKind::Not)
    {
        node.kind = SqlNodeKind::Not;
        node.left = appendExpression<typename E::Child>(program);
    }
    else if constexpr (E::kind == ExprKind::Collection)
    {
        node.kind = SqlNodeKind::Collection;
        node.operation = static_cast<unsigned>(E::operation);
        node.relation = E::relationName;
        if constexpr (!std::same_as<typename E::Child, void>)
            node.left = appendExpression<typename E::Child>(program);
    }
    else
        static_assert(sizeof(E) == 0, "ORM_QUERY_STATIC_SQL: expression is not eligible for compiled SQL");
    const auto index = program.nodes.size();
    program.nodes.push_back(node);
    return index;
}
template <typename P>
inline constexpr bool eligible = []() consteval
{
    bool result = true;
    if constexpr (P::hasPredicate)
        result = result && staticSqlEligible<typename P::Predicate>;
    if constexpr (P::hasHaving)
        result = result && staticSqlEligible<typename P::Having>;
    visitTypes<typename P::Orders>([&]<typename T>() { result = result && staticSqlEligible<T>; });
    visitTypes<typename P::Projections>([&]<typename T>() { result = result && staticSqlEligible<T>; });
    return result;
}();
template <typename P>
constexpr auto program() -> SqlProgram
{
    SqlProgram result;
    result.operation = static_cast<SqlOperation>(P::operation);
    result.shouldJoin = P::operation == PlanOperation::Select && P::shouldJoin;
    result.isDistinct = P::isDistinct;
    visitTypes<typename P::Assignments>(
        [&]<typename A>() {
            result.assignments.push_back({source<typename A::Source>(), addBinding<typename A::Source::Value>(result)});
        });
    if constexpr (P::hasPredicate)
        result.predicate = appendExpression<typename P::Predicate>(result);
    visitTypes<typename P::Groups>([&]<typename C>() { result.groups.push_back(source<C>()); });
    if constexpr (P::hasHaving)
        result.having = appendExpression<typename P::Having>(result);
    visitTypes<typename P::Orders>(
        [&]<typename O>() { result.orders.push_back({source<typename O::Source>(), O::direction, false, {}}); });
    visitTypes<typename P::Projections>(
        [&]<typename Projection>()
        { result.projections.push_back({source<typename Projection::Source>(), Projection::alias.view()}); });
    visitTypes<typename P::Includes>(
        [&]<typename I>() { result.includes.push_back(model::detail::reflectedMemberNameStorage<I::member>.view()); });
    result.hasLimit = P::hasLimit;
    result.hasOffset = P::hasOffset;
    result.boundPagination = true;
    if constexpr (P::hasLimit)
        result.limitParameter = addBinding<std::size_t>(result);
    if constexpr (P::hasOffset)
        result.offsetParameter = addBinding<std::size_t>(result);
    return result;
}

[[nodiscard]] consteval auto validSource(const SqlSource& value, model::ModelView model, bool write, bool joins) -> bool
{
    if (value.isAggregate && value.function == AggregateFunction::CountAll)
        return true;
    if (value.pathSize == 0 || value.pathSize > 2)
        return false;
    const auto* column = model.findColumn(value.pathParts[0]);
    if (column == nullptr)
        return false;
    if (value.pathSize == 1)
        return column->kind == model::FieldKind::Scalar && column->type.has_value();
    if (column->kind != model::FieldKind::ToOne)
        return false;
    const auto target = model.resolveTarget(*column);
    if (!target)
        return false;
    const auto* child = target.findColumn(value.pathParts[1]);
    return child != nullptr && child->kind == model::FieldKind::Scalar && child->type.has_value() &&
           (!(write || !joins) || child->isPrimaryKey);
}
[[nodiscard]] consteval auto validKeys(model::ModelView model) -> bool
{
    bool found = false;
    for (const auto& column : model->columns)
        if (column.isPrimaryKey)
        {
            if (column.kind != model::FieldKind::Scalar)
                return false;
            found = true;
        }
    return found;
}
[[nodiscard]] consteval auto validNode(const SqlQueryView& view, std::size_t index, model::ModelView model, bool write,
                                       bool joins, bool collectionAllowed = true) -> bool
{
    if (index == noSqlNode)
        return true;
    const auto& node = view.nodes[index];
    if (node.kind == SqlNodeKind::And || node.kind == SqlNodeKind::Or)
        return validNode(view, node.left, model, write, joins, collectionAllowed) &&
               validNode(view, node.right, model, write, joins, collectionAllowed);
    if (node.kind == SqlNodeKind::Not)
        return validNode(view, node.left, model, write, joins, collectionAllowed);
    if (node.kind != SqlNodeKind::Collection)
        return validSource(node.source, model, write, joins);
    if (!collectionAllowed || !validKeys(model))
        return false;
    const auto* relation = model.findRelation(node.relation);
    if (relation == nullptr || relation->kind == model::RelationKind::ToOne)
        return false;
    const auto target = model.resolveTarget(*relation);
    if (!target || !validKeys(target))
        return false;
    if (relation->kind == model::RelationKind::OneToMany)
    {
        const auto* mapped = target.findRelation(relation->mappedBy);
        if (mapped == nullptr || mapped->kind != model::RelationKind::ToOne)
            return false;
    }
    else
    {
        const auto junction = model.resolveJunction(*relation);
        if (!junction.isConfigured() || junction.ownerColumns.size() != model.primaryKeySize() ||
            junction.targetColumns.size() != target.primaryKeySize())
            return false;
    }
    if (node.operation != static_cast<unsigned>(CollectionOperator::Exists) && node.left == noSqlNode)
        return false;
    for (const auto& column : target->columns)
        if (column.kind == model::FieldKind::ToOne)
        {
            const auto child = target.resolveTarget(column);
            if (!child || !validKeys(child))
                return false;
        }
    return validNode(view, node.left, target, false, true, false);
}
template <db::CompiledSqlFlavor Flavor>
struct CheckingPolicy : db::detail::StaticSqlPolicy<Flavor>
{
    bool* valid;
    [[nodiscard]] constexpr auto quoteIdentifier(std::string_view name) const -> std::string
    {
        if (name.empty() || name.find('\0') != std::string_view::npos ||
            (Flavor == db::CompiledSqlFlavor::PostgreSQL && name.size() > 63))
            *valid = false;
        std::string result{"\""};
        for (const auto character : name)
        {
            if (character == '"')
                result += '"';
            result += character;
        }
        result += '"';
        return result;
    }
};
template <typename Schema, typename P, db::CompiledSqlFlavor Flavor>
consteval auto valid() -> bool
{
    const auto data = program<P>();
    const auto view = data.view();
    constexpr auto model = model::modelView<Schema, typename P::Model>();
    const auto write = P::operation != PlanOperation::Select;
    if constexpr (P::operation == PlanOperation::Update)
        if (data.assignments.empty() || view.predicate == noSqlNode)
            return false;
    if constexpr (P::operation == PlanOperation::Remove)
        if (view.predicate == noSqlNode)
            return false;
    if (!validNode(view, view.predicate, model, write, view.shouldJoin) ||
        !validNode(view, view.having, model, false, view.shouldJoin))
        return false;
    for (const auto& item : view.groups)
        if (!validSource(item, model, false, view.shouldJoin))
            return false;
    for (const auto& item : view.orders)
        if (!validSource(item.source, model, false, view.shouldJoin))
            return false;
    for (const auto& item : view.projections)
        if (!validSource(item.source, model, false, view.shouldJoin))
            return false;
    for (const auto& item : view.assignments)
        if (!validSource(item.source, model, true, false))
            return false;
    for (const auto& column : model->columns)
        if (column.kind == model::FieldKind::ToOne && !model.resolveTarget(column))
            return false;
    bool result = true;
    (void)emitSql(view, model, CheckingPolicy<Flavor>{{}, &result});
    return result;
}

template <std::size_t Nodes, std::size_t Orders, std::size_t Groups, std::size_t Projections, std::size_t Assignments,
          std::size_t Includes, std::size_t Bindings>
struct FrozenProgram
{
    std::array<SqlNode, Nodes> nodes{};
    std::array<SqlOrder, Orders> orders{};
    std::array<SqlSource, Groups> groups{};
    std::array<SqlProjection, Projections> projections{};
    std::array<SqlAssignment, Assignments> assignments{};
    std::array<std::string_view, Includes> includes{};
    std::array<SqlBindingDescriptor, Bindings> bindings{};
    SqlOperation operation{};
    std::size_t predicate = noSqlNode;
    std::size_t having = noSqlNode;
    bool isDistinct{};
    bool shouldJoin = true;
    bool hasLimit{};
    bool hasOffset{};
    std::size_t limitParameter{};
    std::size_t offsetParameter{};
    [[nodiscard]] constexpr auto view() const -> SqlQueryView
    {
        return {operation,   nodes,          predicate,       having,     orders,     groups,   projections,
                assignments, includes,       bindings,        isDistinct, shouldJoin, hasLimit, hasOffset,
                true,        limitParameter, offsetParameter, 0,          0};
    }
};
template <typename P>
consteval auto freeze()
{
    constexpr auto sizes = []() consteval
    {
        auto data = program<P>();
        return std::array{data.nodes.size(),       data.orders.size(),   data.groups.size(),  data.projections.size(),
                          data.assignments.size(), data.includes.size(), data.bindings.size()};
    }();
    FrozenProgram<sizes[0], sizes[1], sizes[2], sizes[3], sizes[4], sizes[5], sizes[6]> result;
    const auto data = program<P>();
    std::ranges::copy(data.nodes, result.nodes.begin());
    std::ranges::copy(data.orders, result.orders.begin());
    std::ranges::copy(data.groups, result.groups.begin());
    std::ranges::copy(data.projections, result.projections.begin());
    std::ranges::copy(data.assignments, result.assignments.begin());
    std::ranges::copy(data.includes, result.includes.begin());
    std::ranges::copy(data.bindings, result.bindings.begin());
    result.operation = data.operation;
    result.predicate = data.predicate;
    result.having = data.having;
    result.isDistinct = data.isDistinct;
    result.shouldJoin = data.shouldJoin;
    result.hasLimit = data.hasLimit;
    result.hasOffset = data.hasOffset;
    result.limitParameter = data.limitParameter;
    result.offsetParameter = data.offsetParameter;
    return result;
}
template <typename Frozen, std::size_t Length>
struct CompiledStatement
{
    Frozen program{};
    std::array<char, Length + 1> sql{};
    bool valid{};
    [[nodiscard]] constexpr auto view() const -> std::string_view
    {
        return {sql.data(), Length};
    }
};
template <typename Schema, typename P, db::CompiledSqlFlavor Flavor>
consteval auto compile()
{
    constexpr bool accepted = valid<Schema, P, Flavor>();
    constexpr auto length = []() consteval
    {
        if constexpr (accepted)
        {
            auto data = program<P>();
            return emitSql(data.view(), model::modelView<Schema, typename P::Model>(),
                           db::detail::StaticSqlPolicy<Flavor>{})
                .size();
        }
        else
            return std::size_t{};
    }();
    using Frozen = decltype(freeze<P>());
    CompiledStatement<Frozen, length> result;
    result.program = freeze<P>();
    result.valid = accepted;
    if constexpr (accepted)
    {
        const auto value = emitSql(result.program.view(), model::modelView<Schema, typename P::Model>(),
                                   db::detail::StaticSqlPolicy<Flavor>{});
        std::ranges::copy(value, result.sql.begin());
    }
    return result;
}
} // namespace compiled

template <typename P>
inline constexpr bool compiledSqlEligible = compiled::eligible<P>;
template <typename Schema, typename P, db::CompiledSqlFlavor Flavor>
    requires compiledSqlEligible<P>
inline constexpr auto compiledStatement = compiled::compile<Schema, P, Flavor>();

template <typename P, typename Args>
    requires compiledSqlEligible<P>
auto collectParameters(const P& plan, const Args& args) -> std::vector<db::StatementParameter>
{
    std::vector<db::StatementParameter> result;
    const auto& state = PlanAccess::get(plan);
    auto append = [&](std::optional<QueryValue> value, model::ColumnType logicalType)
    {
        const auto nullType = value.has_value() ? std::optional<model::ColumnType>{} : std::optional{logicalType};
        result.push_back({.name = db::detail::parameterName({logicalType, result.size(), {}}),
                          .value = std::move(value),
                          .nullType = nullType});
    };
    std::apply(
        [&](const auto&... assignment)
        {
            [[maybe_unused]] auto add = [&]<typename A>(const A& item)
            {
                using C = typename A::Source;
                const auto& value = resolveValue(item.value, args);
                constexpr auto type = model::LogicalTypeTraits<typename C::Value>::value;
                using V = std::remove_cvref_t<decltype(value)>;
                if constexpr (std::same_as<V, std::nullopt_t> || std::same_as<V, std::nullptr_t>)
                    append(std::nullopt, type);
                else if constexpr (model::isNullable<V>)
                {
                    if (value.has_value())
                        append(typedValue<typename C::Value>(*value), type);
                    else
                        append(std::nullopt, type);
                }
                else
                    append(typedValue<typename C::Value>(value), type);
            };
            (add(assignment), ...);
        },
        std::get<5>(state));
    auto addValue = [&](QueryValue value)
    {
        const auto type = value.getLogicalType();
        append(std::move(value), type);
    };
    if constexpr (P::hasPredicate)
        visitValues(std::get<0>(state), args, addValue);
    if constexpr (P::hasHaving)
        visitValues(std::get<3>(state), args, addValue);
    [[maybe_unused]] auto addPagination = [&](const auto& stored)
    {
        const auto value = static_cast<std::size_t>(resolveValue(stored, args));
        if (value > static_cast<std::size_t>(std::numeric_limits<long long>::max()))
            throw std::out_of_range{"Pagination exceeds the supported signed 64-bit range"};
        addValue(QueryValue{value});
    };
    if constexpr (P::hasLimit)
        addPagination(std::get<6>(state));
    if constexpr (P::hasOffset)
        addPagination(std::get<7>(state));
    return result;
}
} // namespace orm::query::detail
