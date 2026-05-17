#pragma once

#include <cstddef>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "QueryValue.hpp"

namespace orm::query
{
class Predicate;
struct PredicateNode;

using PredicateNodePtr = std::shared_ptr<const PredicateNode>;

enum class ComparisonOperator
{
    Equal,
    NotEqual,
    Greater,
    GreaterOrEqual,
    Less,
    LessOrEqual,
    Like,
    NotLike,
};

enum class NullOperator
{
    IsNull,
    IsNotNull,
};

enum class ListOperator
{
    In,
    NotIn,
};

enum class BetweenOperator
{
    Between,
    NotBetween,
};

enum class LogicalOperator
{
    And,
    Or,
};

class Column
{
public:
    explicit Column(std::string path) : path{std::move(path)} {}

    [[nodiscard]] auto getPath() const -> const std::string&
    {
        return path;
    }

    template <typename T>
    auto operator==(T value) const -> Predicate;

    auto operator==(std::nullptr_t) const -> Predicate;

    template <typename T>
    auto operator!=(T value) const -> Predicate;

    auto operator!=(std::nullptr_t) const -> Predicate;

    template <typename T>
    auto operator>(T value) const -> Predicate;

    template <typename T>
    auto operator>=(T value) const -> Predicate;

    template <typename T>
    auto operator<(T value) const -> Predicate;

    template <typename T>
    auto operator<=(T value) const -> Predicate;

    template <typename T>
    auto like(T value) const -> Predicate;

    template <typename T>
    auto notLike(T value) const -> Predicate;

    auto isNull() const -> Predicate;

    auto isNotNull() const -> Predicate;

    template <typename T>
    auto in(std::initializer_list<T> values) const -> Predicate;

    template <typename T>
    auto in(const std::vector<T>& values) const -> Predicate;

    auto in(std::vector<QueryValue> values) const -> Predicate;

    template <typename T>
    auto notIn(std::initializer_list<T> values) const -> Predicate;

    template <typename T>
    auto notIn(const std::vector<T>& values) const -> Predicate;

    auto notIn(std::vector<QueryValue> values) const -> Predicate;

    template <typename Lower, typename Upper>
    auto between(Lower lowerValue, Upper upperValue) const -> Predicate;

    template <typename Lower, typename Upper>
    auto notBetween(Lower lowerValue, Upper upperValue) const -> Predicate;

private:
    std::string path;

    auto compare(ComparisonOperator comparisonOperator, QueryValue value) const -> Predicate;
    auto list(ListOperator listOperator, std::vector<QueryValue> values) const -> Predicate;

    template <typename Container>
    static auto makeValues(const Container& values) -> std::vector<QueryValue>;
};

struct ComparisonExpression
{
    Column column;
    ComparisonOperator comparisonOperator;
    QueryValue value;
};

struct NullExpression
{
    Column column;
    NullOperator nullOperator;
};

struct ListExpression
{
    Column column;
    ListOperator listOperator;
    std::vector<QueryValue> values;
};

struct BetweenExpression
{
    Column column;
    BetweenOperator betweenOperator;
    QueryValue lowerValue;
    QueryValue upperValue;
};

struct LogicalExpression
{
    PredicateNodePtr left;
    LogicalOperator logicalOperator;
    PredicateNodePtr right;
};

struct NotExpression
{
    PredicateNodePtr predicate;
};

struct RawExpression
{
    std::string sql;
    std::vector<QueryParameter> parameters;
};

struct PredicateNode
{
    using Expression = std::variant<ComparisonExpression, NullExpression, ListExpression, BetweenExpression,
                                    LogicalExpression, NotExpression, RawExpression>;

    Expression expression;
};

class Predicate
{
public:
    explicit Predicate(PredicateNode node) : node{std::make_shared<PredicateNode>(std::move(node))} {}

    [[nodiscard]] auto getNode() const -> const PredicateNode&
    {
        return *node;
    }

private:
    PredicateNodePtr node;

    friend auto operator&&(const Predicate& left, const Predicate& right) -> Predicate;
    friend auto operator||(const Predicate& left, const Predicate& right) -> Predicate;
    friend auto operator!(const Predicate& predicate) -> Predicate;
};

template <typename T>
auto Column::operator==(T value) const -> Predicate
{
    return compare(ComparisonOperator::Equal, QueryValue{std::move(value)});
}

inline auto Column::operator==(std::nullptr_t) const -> Predicate
{
    return isNull();
}

template <typename T>
auto Column::operator!=(T value) const -> Predicate
{
    return compare(ComparisonOperator::NotEqual, QueryValue{std::move(value)});
}

inline auto Column::operator!=(std::nullptr_t) const -> Predicate
{
    return isNotNull();
}

template <typename T>
auto Column::operator>(T value) const -> Predicate
{
    return compare(ComparisonOperator::Greater, QueryValue{std::move(value)});
}

template <typename T>
auto Column::operator>=(T value) const -> Predicate
{
    return compare(ComparisonOperator::GreaterOrEqual, QueryValue{std::move(value)});
}

template <typename T>
auto Column::operator<(T value) const -> Predicate
{
    return compare(ComparisonOperator::Less, QueryValue{std::move(value)});
}

template <typename T>
auto Column::operator<=(T value) const -> Predicate
{
    return compare(ComparisonOperator::LessOrEqual, QueryValue{std::move(value)});
}

template <typename T>
auto Column::like(T value) const -> Predicate
{
    return compare(ComparisonOperator::Like, QueryValue{std::move(value)});
}

template <typename T>
auto Column::notLike(T value) const -> Predicate
{
    return compare(ComparisonOperator::NotLike, QueryValue{std::move(value)});
}

inline auto Column::isNull() const -> Predicate
{
    return Predicate{PredicateNode{NullExpression{.column = *this, .nullOperator = NullOperator::IsNull}}};
}

inline auto Column::isNotNull() const -> Predicate
{
    return Predicate{PredicateNode{NullExpression{.column = *this, .nullOperator = NullOperator::IsNotNull}}};
}

template <typename T>
auto Column::in(std::initializer_list<T> values) const -> Predicate
{
    return in(makeValues(values));
}

template <typename T>
auto Column::in(const std::vector<T>& values) const -> Predicate
{
    return in(makeValues(values));
}

inline auto Column::in(std::vector<QueryValue> values) const -> Predicate
{
    return list(ListOperator::In, std::move(values));
}

template <typename T>
auto Column::notIn(std::initializer_list<T> values) const -> Predicate
{
    return notIn(makeValues(values));
}

template <typename T>
auto Column::notIn(const std::vector<T>& values) const -> Predicate
{
    return notIn(makeValues(values));
}

inline auto Column::notIn(std::vector<QueryValue> values) const -> Predicate
{
    return list(ListOperator::NotIn, std::move(values));
}

template <typename Lower, typename Upper>
auto Column::between(Lower lowerValue, Upper upperValue) const -> Predicate
{
    return Predicate{PredicateNode{BetweenExpression{.column = *this,
                                                     .betweenOperator = BetweenOperator::Between,
                                                     .lowerValue = QueryValue{std::move(lowerValue)},
                                                     .upperValue = QueryValue{std::move(upperValue)}}}};
}

template <typename Lower, typename Upper>
auto Column::notBetween(Lower lowerValue, Upper upperValue) const -> Predicate
{
    return Predicate{PredicateNode{BetweenExpression{.column = *this,
                                                     .betweenOperator = BetweenOperator::NotBetween,
                                                     .lowerValue = QueryValue{std::move(lowerValue)},
                                                     .upperValue = QueryValue{std::move(upperValue)}}}};
}

inline auto Column::compare(ComparisonOperator comparisonOperator, QueryValue value) const -> Predicate
{
    return Predicate{
        PredicateNode{ComparisonExpression{.column = *this, .comparisonOperator = comparisonOperator, .value = value}}};
}

inline auto Column::list(ListOperator listOperator, std::vector<QueryValue> values) const -> Predicate
{
    if (values.empty())
    {
        throw std::invalid_argument{"IN predicate requires at least one value"};
    }

    return Predicate{
        PredicateNode{ListExpression{.column = *this, .listOperator = listOperator, .values = std::move(values)}}};
}

template <typename Container>
auto Column::makeValues(const Container& values) -> std::vector<QueryValue>
{
    std::vector<QueryValue> queryValues;
    queryValues.reserve(values.size());

    for (const auto& value : values)
    {
        queryValues.emplace_back(value);
    }

    return queryValues;
}

inline auto col(std::string path) -> Column
{
    return Column{std::move(path)};
}

template <typename Model, typename FieldType>
auto field(std::string path) -> Column
{
    return Column{std::move(path)};
}

inline auto operator&&(const Predicate& left, const Predicate& right) -> Predicate
{
    return Predicate{PredicateNode{
        LogicalExpression{.left = left.node, .logicalOperator = LogicalOperator::And, .right = right.node}}};
}

inline auto operator||(const Predicate& left, const Predicate& right) -> Predicate
{
    return Predicate{PredicateNode{
        LogicalExpression{.left = left.node, .logicalOperator = LogicalOperator::Or, .right = right.node}}};
}

inline auto operator!(const Predicate& predicate) -> Predicate
{
    return Predicate{PredicateNode{NotExpression{.predicate = predicate.node}}};
}

template <typename... Parameters>
auto raw(std::string sql, Parameters... parameters) -> Predicate
{
    return Predicate{PredicateNode{
        RawExpression{.sql = std::move(sql), .parameters = std::vector<QueryParameter>{std::move(parameters)...}}}};
}
} // namespace orm::query
