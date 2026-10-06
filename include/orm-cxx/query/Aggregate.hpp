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
    inline static constexpr bool isAggregatePredicate = true;

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
    inline static constexpr bool nullable = Nullable;
    inline static constexpr bool isAggregate = true;
    template <typename T>
        requires detail::ORM_QUERY_VALUE<Value, T>
    auto operator==(T&& value) const -> TypedAggregatePredicate<Model>
    {
        return compare(detail::ComparisonOperator::Equal, std::forward<T>(value));
    }
    template <typename T>
        requires detail::ORM_QUERY_VALUE<Value, T>
    auto operator!=(T&& value) const -> TypedAggregatePredicate<Model>
    {
        return compare(detail::ComparisonOperator::NotEqual, std::forward<T>(value));
    }
    template <typename T>
        requires detail::ORM_QUERY_VALUE<Value, T> && detail::ORM_QUERY_ORDERABLE<Value>
    auto operator>(T&& value) const -> TypedAggregatePredicate<Model>
    {
        return compare(detail::ComparisonOperator::Greater, std::forward<T>(value));
    }
    template <typename T>
        requires detail::ORM_QUERY_VALUE<Value, T> && detail::ORM_QUERY_ORDERABLE<Value>
    auto operator>=(T&& value) const -> TypedAggregatePredicate<Model>
    {
        return compare(detail::ComparisonOperator::GreaterOrEqual, std::forward<T>(value));
    }
    template <typename T>
        requires detail::ORM_QUERY_VALUE<Value, T> && detail::ORM_QUERY_ORDERABLE<Value>
    auto operator<(T&& value) const -> TypedAggregatePredicate<Model>
    {
        return compare(detail::ComparisonOperator::Less, std::forward<T>(value));
    }
    template <typename T>
        requires detail::ORM_QUERY_VALUE<Value, T> && detail::ORM_QUERY_ORDERABLE<Value>
    auto operator<=(T&& value) const -> TypedAggregatePredicate<Model>
    {
        return compare(detail::ComparisonOperator::LessOrEqual, std::forward<T>(value));
    }

private:
    friend struct detail::TypedAccess;
    explicit TypedAggregate(detail::AggregateExpression value) : data{std::move(value)} {}
    auto runtime() const -> detail::AggregateExpression
    {
        return data;
    }
    template <typename T>
    auto compare(detail::ComparisonOperator op, T&& value) const -> TypedAggregatePredicate<Model>
    {
        return detail::TypedAccess::make<TypedAggregatePredicate<Model>>(
            detail::AggregatePredicate{detail::AggregatePredicateNode{
                detail::AggregateComparisonExpression{data, op, detail::typedValue<Value>(std::forward<T>(value))}}});
    }
    detail::AggregateExpression data;
};

template <auto... Members>
auto count(TypedColumn<Members...> column) -> TypedAggregate<typename TypedColumn<Members...>::Model, long long, false>
{
    using R = TypedAggregate<typename TypedColumn<Members...>::Model, long long, false>;
    return detail::TypedAccess::make<R>(detail::count(detail::erase(column)));
}
template <typename Model>
auto countAll() -> TypedAggregate<Model, long long, false>
{
    return detail::TypedAccess::make<TypedAggregate<Model, long long, false>>(detail::countAll());
}
template <auto... Members>
    requires detail::ORM_QUERY_NUMERIC<typename TypedColumn<Members...>::Value>
auto sum(TypedColumn<Members...> column)
    -> TypedAggregate<
        typename TypedColumn<Members...>::Model,
        std::conditional_t<std::is_integral_v<typename TypedColumn<Members...>::Value>, long long, double>, true>
{
    using V = std::conditional_t<std::is_integral_v<typename TypedColumn<Members...>::Value>, long long, double>;
    using R = TypedAggregate<typename TypedColumn<Members...>::Model, V, true>;
    return detail::TypedAccess::make<R>(detail::sum(detail::erase(column)));
}
template <auto... Members>
    requires detail::ORM_QUERY_NUMERIC<typename TypedColumn<Members...>::Value>
auto avg(TypedColumn<Members...> column) -> TypedAggregate<typename TypedColumn<Members...>::Model, double, true>
{
    using R = TypedAggregate<typename TypedColumn<Members...>::Model, double, true>;
    return detail::TypedAccess::make<R>(detail::avg(detail::erase(column)));
}
template <auto... Members>
    requires detail::ORM_QUERY_ORDERABLE<typename TypedColumn<Members...>::Value>
auto min(TypedColumn<Members...> column)
    -> TypedAggregate<typename TypedColumn<Members...>::Model, typename TypedColumn<Members...>::Value, true>
{
    using R = TypedAggregate<typename TypedColumn<Members...>::Model, typename TypedColumn<Members...>::Value, true>;
    return detail::TypedAccess::make<R>(detail::min(detail::erase(column)));
}
template <auto... Members>
    requires detail::ORM_QUERY_ORDERABLE<typename TypedColumn<Members...>::Value>
auto max(TypedColumn<Members...> column)
    -> TypedAggregate<typename TypedColumn<Members...>::Model, typename TypedColumn<Members...>::Value, true>
{
    using R = TypedAggregate<typename TypedColumn<Members...>::Model, typename TypedColumn<Members...>::Value, true>;
    return detail::TypedAccess::make<R>(detail::max(detail::erase(column)));
}
template <typename L, typename R>
    requires detail::ORM_QUERY_MODEL<TypedAggregatePredicate<R>, L>
auto operator&&(const TypedAggregatePredicate<L>& l, const TypedAggregatePredicate<R>& r) -> TypedAggregatePredicate<L>
{
    return detail::TypedAccess::make<TypedAggregatePredicate<L>>(detail::erase(l) && detail::erase(r));
}
template <typename L, typename R>
    requires detail::ORM_QUERY_MODEL<TypedAggregatePredicate<R>, L>
auto operator||(const TypedAggregatePredicate<L>& l, const TypedAggregatePredicate<R>& r) -> TypedAggregatePredicate<L>
{
    return detail::TypedAccess::make<TypedAggregatePredicate<L>>(detail::erase(l) || detail::erase(r));
}
template <typename M>
auto operator!(const TypedAggregatePredicate<M>& p) -> TypedAggregatePredicate<M>
{
    return detail::TypedAccess::make<TypedAggregatePredicate<M>>(!detail::erase(p));
}
} // namespace orm::query

namespace orm::query::detail
{
template <typename E>
struct IsTypedAggregatePredicate : std::false_type
{
};
template <typename M>
struct IsTypedAggregatePredicate<TypedAggregatePredicate<M>> : std::true_type
{
};
template <typename E, typename M>
concept AggregatePredicateFor = IsTypedAggregatePredicate<std::remove_cvref_t<E>>::value && ORM_QUERY_MODEL<E, M>;
} // namespace orm::query::detail
