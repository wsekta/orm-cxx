#pragma once

#include "Parameters.hpp"
#include "RuntimePredicate.hpp"

namespace orm::query::detail
{
template <ComparisonOperator Op, typename S, typename T>
constexpr auto comparison(const S& source, T&& value);
}

namespace orm::query
{
template <typename Owner, bool WriteSafe, bool ContainsCollection>
class TypedPredicate;
template <typename Owner>
class TypedAggregatePredicate;

enum class ExprKind
{
    Column,
    Comparison,
    Null,
    List,
    Between,
    Logical,
    Not,
    Collection,
    Aggregate,
    Order,
    Projection,
    DynamicPredicate,
    DynamicAggregatePredicate,
    DynamicAggregate,
    DynamicOrder,
    DynamicProjection,
};

template <ExprKind Kind, typename Meta, typename... Children>
class Expression : public Meta
{
public:
    inline static constexpr ExprKind kind = Kind;
    using ParameterTypes = detail::ConcatTuples<detail::ParameterTypes<Children>...>;
    inline static constexpr auto parameterSlots = detail::SlotIndices<ParameterTypes>::value;

    auto dynamic() const
        requires detail::ORM_QUERY_UNBOUND_PARAMETER<Expression>
    {
        return detail::TypedAccess::make<typename Meta::Dynamic>(runtime());
    }

    template <typename T>
        requires(Kind == ExprKind::Aggregate) && detail::ORM_QUERY_VALUE_OR_PARAMETER<typename Meta::Value, T>
    constexpr auto operator==(T&& value) const
    {
        return detail::comparison<detail::ComparisonOperator::Equal>(*this, std::forward<T>(value));
    }

    template <typename T>
        requires(Kind == ExprKind::Aggregate) && detail::ORM_QUERY_VALUE_OR_PARAMETER<typename Meta::Value, T>
    constexpr auto operator!=(T&& value) const
    {
        return detail::comparison<detail::ComparisonOperator::NotEqual>(*this, std::forward<T>(value));
    }

    template <typename T>
        requires(Kind == ExprKind::Aggregate) && detail::ORM_QUERY_VALUE_OR_PARAMETER<typename Meta::Value, T> &&
                detail::ORM_QUERY_ORDERABLE<typename Meta::Value>
    constexpr auto operator>(T&& value) const
    {
        return detail::comparison<detail::ComparisonOperator::Greater>(*this, std::forward<T>(value));
    }

    template <typename T>
        requires(Kind == ExprKind::Aggregate) && detail::ORM_QUERY_VALUE_OR_PARAMETER<typename Meta::Value, T> &&
                detail::ORM_QUERY_ORDERABLE<typename Meta::Value>
    constexpr auto operator>=(T&& value) const
    {
        return detail::comparison<detail::ComparisonOperator::GreaterOrEqual>(*this, std::forward<T>(value));
    }

    template <typename T>
        requires(Kind == ExprKind::Aggregate) && detail::ORM_QUERY_VALUE_OR_PARAMETER<typename Meta::Value, T> &&
                detail::ORM_QUERY_ORDERABLE<typename Meta::Value>
    constexpr auto operator<(T&& value) const
    {
        return detail::comparison<detail::ComparisonOperator::Less>(*this, std::forward<T>(value));
    }

    template <typename T>
        requires(Kind == ExprKind::Aggregate) && detail::ORM_QUERY_VALUE_OR_PARAMETER<typename Meta::Value, T> &&
                detail::ORM_QUERY_ORDERABLE<typename Meta::Value>
    constexpr auto operator<=(T&& value) const
    {
        return detail::comparison<detail::ComparisonOperator::LessOrEqual>(*this, std::forward<T>(value));
    }

private:
    friend struct detail::TypedAccess;
    constexpr explicit Expression(Children... childrenInit) : children{std::move(childrenInit)...} {}
    auto runtime() const
        requires detail::ORM_QUERY_UNBOUND_PARAMETER<Expression>
    {
        return runtime(std::tuple<>{});
    }
    template <typename Args>
    auto runtime(const Args& args) const
    {
        return Meta::render(children, args);
    }
    std::tuple<Children...> children;
};
} // namespace orm::query

namespace orm::query::detail
{
template <typename E>
struct IsExpression : std::false_type
{
};
template <ExprKind K, typename M, typename... C>
struct IsExpression<Expression<K, M, C...>> : std::true_type
{
};
template <typename E>
inline constexpr bool isExpression = IsExpression<std::remove_cvref_t<E>>::value;
template <typename E>
inline constexpr bool staticSqlEligible = []
{
    if constexpr (requires { E::staticSqlEligible; })
        return E::staticSqlEligible;
    else
        return false;
}();
template <ExprKind K, typename Meta, typename... Children>
constexpr auto makeExpression(Children&&... children)
{
    using E = Expression<K, Meta, std::remove_cvref_t<Children>...>;
    return TypedAccess::make<E>(std::forward<Children>(children)...);
}
template <typename E, typename Args>
auto bindExpression(const E& expression, const Args& args)
{
    if constexpr (isExpression<E>)
        return TypedAccess::make<typename E::Dynamic>(TypedAccess::erase(expression, args));
    else
        return expression;
}
template <typename E, typename Args, typename Callback>
auto visitValues(const E& expression, const Args& args, Callback&& callback) -> void
{
    if constexpr (isExpression<E>)
        E::visit(TypedAccess::children(expression), args, callback);
}
template <typename M, bool W = true, bool C = false>
struct ExpressionMeta
{
    using Model = M;
    using Value = void;
    inline static constexpr bool writeSafe = W;
    inline static constexpr bool containsCollection = C;
    inline static constexpr bool nullable = false;
    inline static constexpr bool isPredicate = false;
    inline static constexpr bool isAggregatePredicate = false;
    inline static constexpr bool isAggregate = false;
    inline static constexpr bool isColumn = false;
    inline static constexpr bool isOrder = false;
    inline static constexpr bool isProjection = false;
};
template <typename S, ComparisonOperator Op>
struct ComparisonMeta : ExpressionMeta<typename S::Model, S::writeSafe, S::containsCollection>
{
    using Source = S;
    using Dynamic = std::conditional_t<S::isAggregate, TypedAggregatePredicate<typename S::Model>,
                                       TypedPredicate<typename S::Model, S::writeSafe, S::containsCollection>>;
    inline static constexpr auto operation = Op;
    inline static constexpr bool isPredicate = !S::isAggregate;
    inline static constexpr bool isAggregatePredicate = S::isAggregate;
    inline static constexpr bool staticSqlEligible = detail::staticSqlEligible<S>;
    template <typename Children, typename Args>
    static auto render(const Children& children, const Args& args)
    {
        const auto source = TypedAccess::erase(std::get<0>(children), args);
        const auto value = typedValue<typename S::Value>(resolveValue(std::get<1>(children), args));
        if constexpr (S::isAggregate)
        {
            if constexpr (Op == ComparisonOperator::Equal)
                return source == value;
            else if constexpr (Op == ComparisonOperator::NotEqual)
                return source != value;
            else if constexpr (Op == ComparisonOperator::Greater)
                return source > value;
            else if constexpr (Op == ComparisonOperator::GreaterOrEqual)
                return source >= value;
            else if constexpr (Op == ComparisonOperator::Less)
                return source < value;
            else
                return source <= value;
        }
        else
            return Predicate{PredicateNode{ComparisonExpression{source, Op, value}}};
    }
    template <typename Children, typename Args, typename Callback>
    static auto visit(const Children& children, const Args& args, Callback& callback) -> void
    {
        callback(typedValue<typename S::Value>(resolveValue(std::get<1>(children), args)));
    }
};
template <ComparisonOperator Op, typename S, typename T>
constexpr auto comparison(const S& source, T&& value)
{
    return makeExpression<ExprKind::Comparison, ComparisonMeta<S, Op>>(source, captureValue(std::forward<T>(value)));
}
template <typename S, NullOperator Op>
struct NullMeta : ExpressionMeta<typename S::Model, S::writeSafe, S::containsCollection>
{
    using Source = S;
    using Dynamic = TypedPredicate<typename S::Model, S::writeSafe, S::containsCollection>;
    inline static constexpr auto operation = Op;
    inline static constexpr bool isPredicate = true;
    inline static constexpr bool staticSqlEligible = detail::staticSqlEligible<S>;
    template <typename Children, typename Args>
    static auto render(const Children& children, const Args& args)
    {
        return Predicate{PredicateNode{NullExpression{TypedAccess::erase(std::get<0>(children), args), Op}}};
    }
    template <typename Children, typename Args, typename Callback>
    static auto visit(const Children&, const Args&, Callback&) -> void
    {
    }
};
template <typename S, BetweenOperator Op>
struct BetweenMeta : ExpressionMeta<typename S::Model, S::writeSafe, S::containsCollection>
{
    using Source = S;
    using Dynamic = TypedPredicate<typename S::Model, S::writeSafe, S::containsCollection>;
    inline static constexpr auto operation = Op;
    inline static constexpr bool isPredicate = true;
    inline static constexpr bool staticSqlEligible = detail::staticSqlEligible<S>;
    template <typename Children, typename Args>
    static auto render(const Children& children, const Args& args)
    {
        return Predicate{
            PredicateNode{BetweenExpression{TypedAccess::erase(std::get<0>(children), args), Op,
                                            typedValue<typename S::Value>(resolveValue(std::get<1>(children), args)),
                                            typedValue<typename S::Value>(resolveValue(std::get<2>(children), args))}}};
    }
    template <typename Children, typename Args, typename Callback>
    static auto visit(const Children& children, const Args& args, Callback& callback) -> void
    {
        callback(typedValue<typename S::Value>(resolveValue(std::get<1>(children), args)));
        callback(typedValue<typename S::Value>(resolveValue(std::get<2>(children), args)));
    }
};
template <typename... V>
struct FixedValues
{
    std::tuple<V...> values;
    using ParameterTypes = ConcatTuples<detail::ParameterTypes<V>...>;
};
template <typename T>
struct ListTraits
{
    inline static constexpr bool fixed = false;
    inline static constexpr auto size = std::dynamic_extent;
};
template <typename... V>
struct ListTraits<FixedValues<V...>>
{
    inline static constexpr bool fixed = true;
    inline static constexpr std::size_t size = sizeof...(V);
};
template <typename V, std::size_t N, std::size_t I>
struct ListTraits<Parameter<std::array<V, N>, I>>
{
    inline static constexpr bool fixed = true;
    inline static constexpr std::size_t size = N;
};
template <typename S, ListOperator Op, typename Stored>
struct ListMeta : ExpressionMeta<typename S::Model, S::writeSafe, S::containsCollection>
{
    using Source = S;
    using Values = Stored;
    using Dynamic = TypedPredicate<typename S::Model, S::writeSafe, S::containsCollection>;
    inline static constexpr auto operation = Op;
    inline static constexpr bool isFixedList = ListTraits<Stored>::fixed;
    inline static constexpr auto arity = ListTraits<Stored>::size;
    inline static constexpr bool isPredicate = true;
    inline static constexpr bool staticSqlEligible = detail::staticSqlEligible<S> && isFixedList;
    static_assert(!isFixedList || arity != 0, "ORM_QUERY_EMPTY_IN: IN requires at least one value");
    template <typename Children, typename Args, typename Callback>
    static auto visit(const Children& children, const Args& args, Callback& callback) -> void
    {
        const auto& stored = std::get<1>(children);
        if constexpr (requires { stored.values; })
            std::apply([&](const auto&... value)
                       { (callback(typedValue<typename S::Value>(resolveValue(value, args))), ...); }, stored.values);
        else
        {
            const auto& values = resolveValue(stored, args);
            if (values.empty())
                throw std::invalid_argument{"IN predicate requires at least one value"};
            for (const auto& value : values)
                callback(typedValue<typename S::Value>(
                    static_cast<typename std::remove_cvref_t<decltype(values)>::value_type>(value)));
        }
    }
    template <typename Children, typename Args>
    static auto render(const Children& children, const Args& args)
    {
        std::vector<QueryValue> values;
        auto append = [&](QueryValue value) { values.push_back(std::move(value)); };
        visit(children, args, append);
        return Predicate{
            PredicateNode{ListExpression{TypedAccess::erase(std::get<0>(children), args), Op, std::move(values)}}};
    }
};
template <ListOperator Op, typename S, typename... V>
constexpr auto fixedList(const S& source, V&&... value)
{
    auto values = FixedValues<decltype(captureValue(std::forward<V>(value)))...>{
        std::tuple{captureValue(std::forward<V>(value))...}};
    return makeExpression<ExprKind::List, ListMeta<S, Op, decltype(values)>>(source, std::move(values));
}
template <ListOperator Op, typename S, typename Container>
constexpr auto containerList(const S& source, const Container& values)
{
    using C = std::remove_cvref_t<Container>;
    if constexpr (isParameter<C>)
        return makeExpression<ExprKind::List, ListMeta<S, Op, C>>(source, values);
    else if constexpr (ContainerTraits<C>::fixed)
        return [&]<std::size_t... I>(std::index_sequence<I...>)
        { return fixedList<Op>(source, values[I]...); }(std::make_index_sequence<ContainerTraits<C>::size>{});
    else
    {
        if (values.size() == 0)
            throw std::invalid_argument{"IN predicate requires at least one value"};
        using V = decltype(captureValue(std::declval<typename C::value_type>()));
        std::vector<V> owned;
        owned.reserve(values.size());
        for (const auto& value : values)
            owned.push_back(captureValue(static_cast<typename C::value_type>(value)));
        return makeExpression<ExprKind::List, ListMeta<S, Op, decltype(owned)>>(source, std::move(owned));
    }
}
template <typename L, typename R, LogicalOperator Op>
struct LogicalMeta
    : ExpressionMeta<typename L::Model, L::writeSafe && R::writeSafe, L::containsCollection || R::containsCollection>
{
    using Left = L;
    using Right = R;
    using Dynamic = std::conditional_t<L::isAggregatePredicate, TypedAggregatePredicate<typename L::Model>,
                                       TypedPredicate<typename L::Model, L::writeSafe && R::writeSafe,
                                                      L::containsCollection || R::containsCollection>>;
    inline static constexpr auto operation = Op;
    inline static constexpr bool isPredicate = L::isPredicate;
    inline static constexpr bool isAggregatePredicate = L::isAggregatePredicate;
    inline static constexpr bool staticSqlEligible = detail::staticSqlEligible<L> && detail::staticSqlEligible<R>;
    template <typename Children, typename Args>
    static auto render(const Children& children, const Args& args)
    {
        const auto left = TypedAccess::erase(std::get<0>(children), args);
        const auto right = TypedAccess::erase(std::get<1>(children), args);
        if constexpr (Op == LogicalOperator::And)
            return left && right;
        else
            return left || right;
    }
    template <typename Children, typename Args, typename Callback>
    static auto visit(const Children& children, const Args& args, Callback& callback) -> void
    {
        visitValues(std::get<0>(children), args, callback);
        visitValues(std::get<1>(children), args, callback);
    }
};
template <typename E>
struct NotMeta : ExpressionMeta<typename E::Model, E::writeSafe, E::containsCollection>
{
    using Child = E;
    using Dynamic = std::conditional_t<E::isAggregatePredicate, TypedAggregatePredicate<typename E::Model>,
                                       TypedPredicate<typename E::Model, E::writeSafe, E::containsCollection>>;
    inline static constexpr bool isPredicate = E::isPredicate;
    inline static constexpr bool isAggregatePredicate = E::isAggregatePredicate;
    inline static constexpr bool staticSqlEligible = detail::staticSqlEligible<E>;
    template <typename Children, typename Args>
    static auto render(const Children& children, const Args& args)
    {
        return !TypedAccess::erase(std::get<0>(children), args);
    }
    template <typename Children, typename Args, typename Callback>
    static auto visit(const Children& children, const Args& args, Callback& callback) -> void
    {
        visitValues(std::get<0>(children), args, callback);
    }
};
} // namespace orm::query::detail
