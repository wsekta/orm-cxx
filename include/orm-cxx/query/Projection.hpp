#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "Predicate.hpp"

namespace orm::query
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
    using Expression =
        std::variant<AggregateComparisonExpression, AggregateLogicalExpression, AggregateNotExpression>;

    Expression expression;
};

class AggregatePredicate
{
public:
    explicit AggregatePredicate(AggregatePredicateNode predicateNode) :
        node{std::make_shared<AggregatePredicateNode>(std::move(predicateNode))}
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

using ProjectionSource = std::variant<Column, AggregateExpression>;

/**
 * @brief A projected source expression and the DTO field alias it hydrates.
 */
struct Projection
{
    std::string resultField;
    ProjectionSource source;
};

/**
 * @brief Projects a source model column path into a result DTO field.
 */
inline auto as(std::string resultField, Column sourceColumn) -> Projection
{
    return Projection{.resultField = std::move(resultField), .source = std::move(sourceColumn)};
}

/**
 * @brief Projects an aggregate expression into a result DTO field.
 */
inline auto as(std::string resultField, AggregateExpression aggregate) -> Projection
{
    return Projection{.resultField = std::move(resultField), .source = std::move(aggregate)};
}

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

inline auto AggregateExpression::compare(ComparisonOperator comparisonOperator, QueryValue value) const
    -> AggregatePredicate
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
} // namespace orm::query
