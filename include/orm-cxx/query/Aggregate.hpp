#pragma once

#include <memory>
#include <optional>
#include <utility>
#include <variant>

#include "Predicate.hpp"

namespace orm::query::detail
{
class AggregatePredicate;
struct AggregatePredicateNode;

using AggregatePredicateNodePtr = std::shared_ptr<const AggregatePredicateNode>;

enum class AggregateFunction
{
    Count,
    CountAll,
    Sum,
    Avg,
    Min,
    Max,
};

struct AggregateExpression
{
    AggregateFunction function;
    std::optional<Column> column = std::nullopt;

    template <typename T>
    auto operator==(T value) const -> AggregatePredicate;

    template <typename T>
    auto operator!=(T value) const -> AggregatePredicate;

    template <typename T>
    auto operator>(T value) const -> AggregatePredicate;

    template <typename T>
    auto operator>=(T value) const -> AggregatePredicate;

    template <typename T>
    auto operator<(T value) const -> AggregatePredicate;

    template <typename T>
    auto operator<=(T value) const -> AggregatePredicate;

private:
    auto compare(ComparisonOperator comparisonOperator, QueryValue value) const -> AggregatePredicate;
};

struct AggregateComparisonExpression
{
    AggregateExpression aggregate;
    ComparisonOperator comparisonOperator;
    QueryValue value;
};

struct AggregateLogicalExpression
{
    AggregatePredicateNodePtr left;
    LogicalOperator logicalOperator;
    AggregatePredicateNodePtr right;
};

struct AggregateNotExpression
{
    AggregatePredicateNodePtr predicate;
};

struct AggregatePredicateNode
{
    using Expression = std::variant<AggregateComparisonExpression, AggregateLogicalExpression, AggregateNotExpression>;

    Expression expression;
};

class AggregatePredicate
{
public:
    explicit AggregatePredicate(AggregatePredicateNode predicateNode)
        : node{std::make_shared<AggregatePredicateNode>(std::move(predicateNode))}
    {
    }

    [[nodiscard]] auto getNode() const -> const AggregatePredicateNode&
    {
        return *node;
    }

private:
    AggregatePredicateNodePtr node;

    friend auto operator&&(const AggregatePredicate& left, const AggregatePredicate& right) -> AggregatePredicate;
    friend auto operator||(const AggregatePredicate& left, const AggregatePredicate& right) -> AggregatePredicate;
    friend auto operator!(const AggregatePredicate& predicate) -> AggregatePredicate;
};

inline auto count(Column sourceColumn) -> AggregateExpression
{
    return AggregateExpression{.function = AggregateFunction::Count, .column = std::move(sourceColumn)};
}

inline auto countAll() -> AggregateExpression
{
    return AggregateExpression{.function = AggregateFunction::CountAll};
}

inline auto sum(Column sourceColumn) -> AggregateExpression
{
    return AggregateExpression{.function = AggregateFunction::Sum, .column = std::move(sourceColumn)};
}

inline auto avg(Column sourceColumn) -> AggregateExpression
{
    return AggregateExpression{.function = AggregateFunction::Avg, .column = std::move(sourceColumn)};
}

inline auto min(Column sourceColumn) -> AggregateExpression
{
    return AggregateExpression{.function = AggregateFunction::Min, .column = std::move(sourceColumn)};
}

inline auto max(Column sourceColumn) -> AggregateExpression
{
    return AggregateExpression{.function = AggregateFunction::Max, .column = std::move(sourceColumn)};
}

template <typename T>
auto AggregateExpression::operator==(T value) const -> AggregatePredicate
{
    return compare(ComparisonOperator::Equal, QueryValue{std::move(value)});
}

template <typename T>
auto AggregateExpression::operator!=(T value) const -> AggregatePredicate
{
    return compare(ComparisonOperator::NotEqual, QueryValue{std::move(value)});
}

template <typename T>
auto AggregateExpression::operator>(T value) const -> AggregatePredicate
{
    return compare(ComparisonOperator::Greater, QueryValue{std::move(value)});
}

template <typename T>
auto AggregateExpression::operator>=(T value) const -> AggregatePredicate
{
    return compare(ComparisonOperator::GreaterOrEqual, QueryValue{std::move(value)});
}

template <typename T>
auto AggregateExpression::operator<(T value) const -> AggregatePredicate
{
    return compare(ComparisonOperator::Less, QueryValue{std::move(value)});
}

template <typename T>
auto AggregateExpression::operator<=(T value) const -> AggregatePredicate
{
    return compare(ComparisonOperator::LessOrEqual, QueryValue{std::move(value)});
}

inline auto AggregateExpression::compare(ComparisonOperator comparisonOperator,
                                         QueryValue value) const -> AggregatePredicate
{
    return AggregatePredicate{AggregatePredicateNode{
        AggregateComparisonExpression{.aggregate = *this, .comparisonOperator = comparisonOperator, .value = value}}};
}

inline auto operator&&(const AggregatePredicate& left, const AggregatePredicate& right) -> AggregatePredicate
{
    return AggregatePredicate{AggregatePredicateNode{
        AggregateLogicalExpression{.left = left.node, .logicalOperator = LogicalOperator::And, .right = right.node}}};
}

inline auto operator||(const AggregatePredicate& left, const AggregatePredicate& right) -> AggregatePredicate
{
    return AggregatePredicate{AggregatePredicateNode{
        AggregateLogicalExpression{.left = left.node, .logicalOperator = LogicalOperator::Or, .right = right.node}}};
}

inline auto operator!(const AggregatePredicate& predicate) -> AggregatePredicate
{
    return AggregatePredicate{AggregatePredicateNode{AggregateNotExpression{.predicate = predicate.node}}};
}
} // namespace orm::query::detail

namespace orm::query
{
template <typename Owner>
class TypedAggregatePredicate
{
public:
    using Model = Owner;
    using ParameterTypes = std::tuple<>;
    using DynamicAggregateMarker = void;
    inline static constexpr auto kind = ExprKind::DynamicAggregatePredicate;
    inline static constexpr bool isPredicate = false;
    inline static constexpr bool isAggregatePredicate = true;
    inline static constexpr bool writeSafe = false;
    inline static constexpr bool containsCollection = false;
    inline static constexpr bool staticSqlEligible = false;
    template <typename E>
        requires detail::isExpression<E> && E::isAggregatePredicate && detail::ORM_QUERY_MODEL<E, Owner> &&
                 detail::ORM_QUERY_UNBOUND_PARAMETER<E>
    TypedAggregatePredicate(const E& expression) : data{detail::erase(expression)}
    {
    }
    template <typename E>
        requires detail::isExpression<E> && E::isAggregatePredicate && detail::ORM_QUERY_MODEL<E, Owner> &&
                     detail::ORM_QUERY_UNBOUND_PARAMETER<E>
    auto operator=(const E& expression) -> TypedAggregatePredicate&
    {
        data = detail::erase(expression);
        return *this;
    }
    auto dynamic() const -> TypedAggregatePredicate
    {
        return *this;
    }

private:
    friend struct detail::TypedAccess;
    explicit TypedAggregatePredicate(detail::AggregatePredicate value) : data{std::move(value)} {}
    auto runtime() const -> detail::AggregatePredicate
    {
        return data;
    }
    detail::AggregatePredicate data;
};
template <typename Owner, typename ResultValue, bool Nullable>
class TypedAggregate
{
public:
    using Model = Owner;
    using Value = ResultValue;
    using ParameterTypes = std::tuple<>;
    inline static constexpr auto kind = ExprKind::DynamicAggregate;
    inline static constexpr bool nullable = Nullable;
    inline static constexpr bool isAggregate = true;
    inline static constexpr bool writeSafe = false;
    inline static constexpr bool containsCollection = false;
    inline static constexpr bool staticSqlEligible = false;
    template <typename E>
        requires detail::isExpression<E> && E::isAggregate && detail::ORM_QUERY_MODEL<E, Owner> &&
                 std::same_as<typename E::Value, Value> &&
                 (E::nullable == Nullable) && detail::ORM_QUERY_UNBOUND_PARAMETER<E>
    TypedAggregate(const E& expression) : data{detail::erase(expression)}
    {
    }
    template <typename E>
        requires detail::isExpression<E> && E::isAggregate && detail::ORM_QUERY_MODEL<E, Owner> &&
                     std::same_as<typename E::Value, Value> &&
                     (E::nullable == Nullable) && detail::ORM_QUERY_UNBOUND_PARAMETER<E>
    auto operator=(const E& expression) -> TypedAggregate&
    {
        data = detail::erase(expression);
        return *this;
    }
#define ORM_QUERY_DYNAMIC_AGG_COMPARE(symbol, op, ordered)                                                             \
    template <typename T>                                                                                              \
        requires detail::ORM_QUERY_VALUE_OR_PARAMETER<Value, T> && (!(ordered) || detail::ORM_QUERY_ORDERABLE<Value>)  \
    constexpr auto operator symbol(T&& value) const                                                                    \
    {                                                                                                                  \
        return detail::comparison<detail::ComparisonOperator::op>(*this, std::forward<T>(value));                      \
    }
    ORM_QUERY_DYNAMIC_AGG_COMPARE(==, Equal, false)
    ORM_QUERY_DYNAMIC_AGG_COMPARE(!=, NotEqual, false)
    ORM_QUERY_DYNAMIC_AGG_COMPARE(>, Greater, true)
    ORM_QUERY_DYNAMIC_AGG_COMPARE(>=, GreaterOrEqual, true)
    ORM_QUERY_DYNAMIC_AGG_COMPARE(<, Less, true)
    ORM_QUERY_DYNAMIC_AGG_COMPARE(<=, LessOrEqual, true)
#undef ORM_QUERY_DYNAMIC_AGG_COMPARE
    auto dynamic() const -> TypedAggregate
    {
        return *this;
    }

private:
    friend struct detail::TypedAccess;
    explicit TypedAggregate(detail::AggregateExpression value) : data{std::move(value)} {}
    auto runtime() const -> detail::AggregateExpression
    {
        return data;
    }
    detail::AggregateExpression data;
};
} // namespace orm::query

namespace orm::query::detail
{
template <AggregateFunction Function, typename C, typename M, typename V, bool N>
struct AggregateMeta : ExpressionMeta<M, false>
{
    using Source = C;
    using Value = V;
    using Dynamic = TypedAggregate<M, V, N>;
    inline static constexpr auto function = Function;
    inline static constexpr bool nullable = N;
    inline static constexpr bool isAggregate = true;
    inline static constexpr bool staticSqlEligible = true;
    template <typename Children, typename Args>
    static auto render(const Children& children, const Args& args)
    {
        if constexpr (Function == AggregateFunction::CountAll)
            return AggregateExpression{.function = Function};
        else
            return AggregateExpression{.function = Function, .column = TypedAccess::erase(std::get<0>(children), args)};
    }
    template <typename Children, typename Args, typename Callback>
    static auto visit(const Children&, const Args&, Callback&) -> void
    {
    }
};
template <typename M>
struct IsTypedAggregatePredicate<TypedAggregatePredicate<M>> : std::true_type
{
};
template <typename E>
struct IsTypedAggregate : std::false_type
{
};
template <typename M, typename V, bool N>
struct IsTypedAggregate<TypedAggregate<M, V, N>> : std::true_type
{
};
template <typename E>
concept AggregateExpressionType =
    (IsTypedAggregate<std::remove_cvref_t<E>>::value || isExpression<E>) && std::remove_cvref_t<E>::isAggregate;
template <typename E, typename M>
concept AnyAggregatePredicateFor =
    (IsTypedAggregatePredicate<std::remove_cvref_t<E>>::value || isExpression<E>) &&
    std::remove_cvref_t<E>::isAggregatePredicate && ORM_QUERY_MODEL<std::remove_cvref_t<E>, M>;
template <typename E, typename M>
concept AggregatePredicateFor = AnyAggregatePredicateFor<E, M> && ORM_QUERY_UNBOUND_PARAMETER<E>;
template <typename E, typename M>
concept PlanAggregatePredicateFor = AnyAggregatePredicateFor<E, M>;
} // namespace orm::query::detail

namespace orm::query
{
template <auto... Members>
constexpr auto count(TypedColumn<Members...> column)
{
    using C = TypedColumn<Members...>;
    return detail::makeExpression<ExprKind::Aggregate, detail::AggregateMeta<detail::AggregateFunction::Count, C,
                                                                             typename C::Model, long long, false>>(
        column);
}
template <typename Model>
constexpr auto countAll()
{
    return detail::makeExpression<ExprKind::Aggregate, detail::AggregateMeta<detail::AggregateFunction::CountAll, void,
                                                                             Model, long long, false>>();
}
template <auto... Members>
    requires detail::ORM_QUERY_NUMERIC<typename TypedColumn<Members...>::Value>
constexpr auto sum(TypedColumn<Members...> column)
{
    using C = TypedColumn<Members...>;
    using V = std::conditional_t<std::is_integral_v<typename C::Value>, long long, double>;
    return detail::makeExpression<ExprKind::Aggregate,
                                  detail::AggregateMeta<detail::AggregateFunction::Sum, C, typename C::Model, V, true>>(
        column);
}
template <auto... Members>
    requires detail::ORM_QUERY_NUMERIC<typename TypedColumn<Members...>::Value>
constexpr auto avg(TypedColumn<Members...> column)
{
    using C = TypedColumn<Members...>;
    return detail::makeExpression<
        ExprKind::Aggregate, detail::AggregateMeta<detail::AggregateFunction::Avg, C, typename C::Model, double, true>>(
        column);
}
template <auto... Members>
    requires detail::ORM_QUERY_ORDERABLE<typename TypedColumn<Members...>::Value>
constexpr auto min(TypedColumn<Members...> column)
{
    using C = TypedColumn<Members...>;
    return detail::makeExpression<
        ExprKind::Aggregate,
        detail::AggregateMeta<detail::AggregateFunction::Min, C, typename C::Model, typename C::Value, true>>(column);
}
template <auto... Members>
    requires detail::ORM_QUERY_ORDERABLE<typename TypedColumn<Members...>::Value>
constexpr auto max(TypedColumn<Members...> column)
{
    using C = TypedColumn<Members...>;
    return detail::makeExpression<
        ExprKind::Aggregate,
        detail::AggregateMeta<detail::AggregateFunction::Max, C, typename C::Model, typename C::Value, true>>(column);
}
} // namespace orm::query
