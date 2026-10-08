#pragma once

#include "Expression.hpp"

namespace orm::query::detail
{
template <auto... Members>
struct ColumnTraits;
template <auto Member>
struct ColumnTraits<Member>
{
    static_assert(std::is_member_object_pointer_v<decltype(Member)>,
                  "ORM_QUERY_MEMBER: expected a data member pointer");
    using Model = model::detail::member_owner_t<Member>;
    using Field = model::detail::member_value_t<Member>;
    using Value = scalar_t<Field>;
    static_assert(Member != nullptr && model::detail::memberExists<Model, Member>(),
                  "ORM_QUERY_MEMBER: expected a reflected aggregate member");
    static_assert(supportedScalar<Value>, "ORM_QUERY_COLUMN: terminal field must be a supported scalar");
    inline static constexpr bool nullable = model::isNullable<Field>;
    inline static constexpr bool writeSafe = true;
    inline static constexpr std::array pathParts{model::detail::reflectedMemberNameStorage<Member>.view()};
    static auto path() -> std::string
    {
        return std::string{pathParts[0]};
    }
};
template <auto Relation, auto Member>
struct ColumnTraits<Relation, Member>
{
    static_assert(std::is_member_object_pointer_v<decltype(Relation)> &&
                      std::is_member_object_pointer_v<decltype(Member)>,
                  "ORM_QUERY_MEMBER: expected data member pointers");
    using Model = model::detail::member_owner_t<Relation>;
    using RelationField = model::detail::member_value_t<Relation>;
    using Target = scalar_t<RelationField>;
    using Terminal = ColumnTraits<Member>;
    using Value = typename Terminal::Value;
    static_assert(Relation != nullptr && model::detail::memberExists<Model, Relation>(),
                  "ORM_QUERY_MEMBER: expected a reflected relation member");
    static_assert(std::same_as<Target, typename Terminal::Model> && std::is_aggregate_v<Target> &&
                      !is_relation_collection_v<RelationField>,
                  "ORM_QUERY_PATH: adjacent owners must follow a to-one relation");
    inline static constexpr bool nullable = model::isNullable<RelationField> || Terminal::nullable;
    inline static constexpr bool writeSafe =
        model::detail::isPrimaryKey<Target>(model::detail::reflectedMemberNameStorage<Member>.view());
    inline static constexpr std::array pathParts{model::detail::reflectedMemberNameStorage<Relation>.view(),
                                                 model::detail::reflectedMemberNameStorage<Member>.view()};
    static auto path() -> std::string
    {
        return std::string{pathParts[0]} + "." + std::string{pathParts[1]};
    }
};
template <auto Member>
consteval auto collectionMember() -> bool
{
    if constexpr (!std::is_member_object_pointer_v<decltype(Member)>)
        return false;
    else
        return Member != nullptr && is_relation_collection_v<model::detail::member_value_t<Member>>;
}
template <auto Member>
concept ORM_QUERY_COLLECTION = collectionMember<Member>();
template <bool C>
concept ORM_QUERY_NO_NESTED_COLLECTION = !C;
template <auto Member>
struct CollectionTraits
{
    using Model = model::detail::member_owner_t<Member>;
    using Target = relation_target_t<model::detail::member_value_t<Member>>;
    static_assert(model::detail::memberExists<Model, Member>(), "ORM_QUERY_MEMBER: expected a reflected collection");
    static auto name() -> std::string
    {
        return std::string{model::detail::reflectedMemberNameStorage<Member>.view()};
    }
};
} // namespace orm::query::detail

namespace orm::query
{
template <typename Owner, bool WriteSafe = true, bool ContainsCollection = false>
class TypedPredicate
{
public:
    using Model = Owner;
    using ParameterTypes = std::tuple<>;
    inline static constexpr auto kind = ExprKind::DynamicPredicate;
    inline static constexpr bool isPredicate = true;
    inline static constexpr bool isAggregatePredicate = false;
    inline static constexpr bool isAggregate = false;
    inline static constexpr bool writeSafe = WriteSafe;
    inline static constexpr bool containsCollection = ContainsCollection;
    inline static constexpr bool staticSqlEligible = false;
    template <typename E>
        requires detail::isExpression<E> && E::isPredicate && detail::ORM_QUERY_MODEL<E, Owner> &&
                 (E::writeSafe == WriteSafe) &&
                 (E::containsCollection == ContainsCollection) && detail::ORM_QUERY_UNBOUND_PARAMETER<E>
    TypedPredicate(const E& expression) : data{detail::erase(expression)}
    {
    }
    template <typename E>
        requires detail::isExpression<E> && E::isPredicate && detail::ORM_QUERY_MODEL<E, Owner> &&
                     (E::writeSafe == WriteSafe) &&
                     (E::containsCollection == ContainsCollection) && detail::ORM_QUERY_UNBOUND_PARAMETER<E>
    auto operator=(const E& expression) -> TypedPredicate&
    {
        data = detail::erase(expression);
        return *this;
    }
    auto dynamic() const -> TypedPredicate
    {
        return *this;
    }

private:
    friend struct detail::TypedAccess;
    explicit TypedPredicate(detail::Predicate value) : data{std::move(value)} {}
    auto runtime() const -> detail::Predicate
    {
        return data;
    }
    detail::Predicate data;
};

template <auto... Members>
class TypedColumn
{
    static_assert(sizeof...(Members) >= 1 && sizeof...(Members) <= 2,
                  "ORM_QUERY_PATH: columns support a direct member or one to-one relation level");
    using Traits = detail::ColumnTraits<Members...>;

public:
    using Model = typename Traits::Model;
    using Value = typename Traits::Value;
    using ParameterTypes = std::tuple<>;
    inline static constexpr auto kind = ExprKind::Column;
    inline static constexpr auto pathParts = Traits::pathParts;
    inline static constexpr bool isColumn = true;
    inline static constexpr bool isAggregate = false;
    inline static constexpr bool nullable = Traits::nullable;
    inline static constexpr bool writeSafe = Traits::writeSafe;
    inline static constexpr bool containsCollection = false;
    inline static constexpr bool staticSqlEligible = true;
    template <typename T>
        requires detail::ORM_QUERY_VALUE_OR_PARAMETER<Value, T>
    constexpr auto operator==(T&& value) const
    {
        return detail::comparison<detail::ComparisonOperator::Equal>(*this, std::forward<T>(value));
    }

    template <typename T>
        requires detail::ORM_QUERY_VALUE_OR_PARAMETER<Value, T>
    constexpr auto operator!=(T&& value) const
    {
        return detail::comparison<detail::ComparisonOperator::NotEqual>(*this, std::forward<T>(value));
    }

    template <typename T>
        requires detail::ORM_QUERY_VALUE_OR_PARAMETER<Value, T> && detail::ORM_QUERY_ORDERABLE<Value>
    constexpr auto operator>(T&& value) const
    {
        return detail::comparison<detail::ComparisonOperator::Greater>(*this, std::forward<T>(value));
    }

    template <typename T>
        requires detail::ORM_QUERY_VALUE_OR_PARAMETER<Value, T> && detail::ORM_QUERY_ORDERABLE<Value>
    constexpr auto operator>=(T&& value) const
    {
        return detail::comparison<detail::ComparisonOperator::GreaterOrEqual>(*this, std::forward<T>(value));
    }

    template <typename T>
        requires detail::ORM_QUERY_VALUE_OR_PARAMETER<Value, T> && detail::ORM_QUERY_ORDERABLE<Value>
    constexpr auto operator<(T&& value) const
    {
        return detail::comparison<detail::ComparisonOperator::Less>(*this, std::forward<T>(value));
    }

    template <typename T>
        requires detail::ORM_QUERY_VALUE_OR_PARAMETER<Value, T> && detail::ORM_QUERY_ORDERABLE<Value>
    constexpr auto operator<=(T&& value) const
    {
        return detail::comparison<detail::ComparisonOperator::LessOrEqual>(*this, std::forward<T>(value));
    }
    template <typename T>
        requires detail::ORM_QUERY_VALUE_OR_PARAMETER<Value, T> && detail::ORM_QUERY_STRING<Value>
    constexpr auto like(T&& value) const
    {
        return detail::comparison<detail::ComparisonOperator::Like>(*this, std::forward<T>(value));
    }
    template <typename T>
        requires detail::ORM_QUERY_VALUE_OR_PARAMETER<Value, T> && detail::ORM_QUERY_STRING<Value>
    constexpr auto notLike(T&& value) const
    {
        return detail::comparison<detail::ComparisonOperator::NotLike>(*this, std::forward<T>(value));
    }
    constexpr auto isNull() const
        requires detail::ORM_QUERY_NULLABLE<nullable>
    {
        return detail::makeExpression<ExprKind::Null, detail::NullMeta<TypedColumn, detail::NullOperator::IsNull>>(
            *this);
    }
    constexpr auto isNotNull() const
        requires detail::ORM_QUERY_NULLABLE<nullable>
    {
        return detail::makeExpression<ExprKind::Null, detail::NullMeta<TypedColumn, detail::NullOperator::IsNotNull>>(
            *this);
    }
    constexpr auto operator==(std::nullptr_t) const
        requires detail::ORM_QUERY_NULLABLE<nullable>
    {
        return isNull();
    }
    constexpr auto operator!=(std::nullptr_t) const
        requires detail::ORM_QUERY_NULLABLE<nullable>
    {
        return isNotNull();
    }
    constexpr auto operator==(std::nullopt_t) const
        requires detail::ORM_QUERY_NULLABLE<nullable>
    {
        return isNull();
    }
    constexpr auto operator!=(std::nullopt_t) const
        requires detail::ORM_QUERY_NULLABLE<nullable>
    {
        return isNotNull();
    }
    template <typename T>
        requires detail::ORM_QUERY_VALUE_OR_PARAMETER<Value, T>
    auto in(std::initializer_list<T> values) const
    {
        return detail::containerList<detail::ListOperator::In>(*this, values);
    }
    template <typename T>
        requires detail::ContainerTraits<detail::parameter_value_t<T>>::isContainer &&
                 detail::ORM_QUERY_VALUE_OR_PARAMETER<
                     Value, typename detail::ContainerTraits<detail::parameter_value_t<T>>::Value>
    constexpr auto in(const T& values) const
    {
        return detail::containerList<detail::ListOperator::In>(*this, values);
    }
    template <typename T, std::size_t N>
        requires detail::ORM_QUERY_VALUE_OR_PARAMETER<Value, T>
    constexpr auto in(const T (&values)[N]) const
    {
        return [&]<std::size_t... I>(std::index_sequence<I...>)
        { return detail::fixedList<detail::ListOperator::In>(*this, values[I]...); }(std::make_index_sequence<N>{});
    }
    template <typename... T>
        requires(sizeof...(T) > 0) && (detail::ORM_QUERY_VALUE_OR_PARAMETER<Value, T> && ...)
    constexpr auto in(T&&... values) const
    {
        return detail::fixedList<detail::ListOperator::In>(*this, std::forward<T>(values)...);
    }

    template <typename T>
        requires detail::ORM_QUERY_VALUE_OR_PARAMETER<Value, T>
    auto notIn(std::initializer_list<T> values) const
    {
        return detail::containerList<detail::ListOperator::NotIn>(*this, values);
    }
    template <typename T>
        requires detail::ContainerTraits<detail::parameter_value_t<T>>::isContainer &&
                 detail::ORM_QUERY_VALUE_OR_PARAMETER<
                     Value, typename detail::ContainerTraits<detail::parameter_value_t<T>>::Value>
    constexpr auto notIn(const T& values) const
    {
        return detail::containerList<detail::ListOperator::NotIn>(*this, values);
    }
    template <typename T, std::size_t N>
        requires detail::ORM_QUERY_VALUE_OR_PARAMETER<Value, T>
    constexpr auto notIn(const T (&values)[N]) const
    {
        return [&]<std::size_t... I>(std::index_sequence<I...>)
        { return detail::fixedList<detail::ListOperator::NotIn>(*this, values[I]...); }(std::make_index_sequence<N>{});
    }
    template <typename... T>
        requires(sizeof...(T) > 0) && (detail::ORM_QUERY_VALUE_OR_PARAMETER<Value, T> && ...)
    constexpr auto notIn(T&&... values) const
    {
        return detail::fixedList<detail::ListOperator::NotIn>(*this, std::forward<T>(values)...);
    }
    template <typename L, typename U>
        requires detail::ORM_QUERY_VALUE_OR_PARAMETER<Value, L> && detail::ORM_QUERY_VALUE_OR_PARAMETER<Value, U> &&
                 detail::ORM_QUERY_ORDERABLE<Value>
    constexpr auto between(L&& lower, U&& upper) const
    {
        return detail::makeExpression<ExprKind::Between,
                                      detail::BetweenMeta<TypedColumn, detail::BetweenOperator::Between>>(
            *this, detail::captureValue(std::forward<L>(lower)), detail::captureValue(std::forward<U>(upper)));
    }
    template <typename L, typename U>
        requires detail::ORM_QUERY_VALUE_OR_PARAMETER<Value, L> && detail::ORM_QUERY_VALUE_OR_PARAMETER<Value, U> &&
                 detail::ORM_QUERY_ORDERABLE<Value>
    constexpr auto notBetween(L&& lower, U&& upper) const
    {
        return detail::makeExpression<ExprKind::Between,
                                      detail::BetweenMeta<TypedColumn, detail::BetweenOperator::NotBetween>>(
            *this, detail::captureValue(std::forward<L>(lower)), detail::captureValue(std::forward<U>(upper)));
    }

private:
    friend struct detail::TypedAccess;
    constexpr TypedColumn() = default;
    auto runtime() const -> detail::Column
    {
        return detail::col(Traits::path());
    }
};
template <auto... Members>
    requires detail::ORM_QUERY_MEMBER<Members...>
constexpr auto col() -> TypedColumn<Members...>
{
    return detail::TypedAccess::make<TypedColumn<Members...>>();
}
} // namespace orm::query

namespace orm::query::detail
{
template <typename E>
concept ORM_QUERY_WRITE_SAFE = E::writeSafe;
template <typename E>
struct IsTypedPredicate : std::false_type
{
};
template <typename M, bool W, bool C>
struct IsTypedPredicate<TypedPredicate<M, W, C>> : std::true_type
{
};
template <typename E>
struct IsTypedColumn : std::false_type
{
};
template <auto... Members>
struct IsTypedColumn<TypedColumn<Members...>> : std::true_type
{
};
template <typename E>
concept PredicateExpression =
    (IsTypedPredicate<std::remove_cvref_t<E>>::value || isExpression<E>) && std::remove_cvref_t<E>::isPredicate;
template <typename E, typename M>
concept AnyPredicateFor = PredicateExpression<E> && ORM_QUERY_MODEL<std::remove_cvref_t<E>, M>;
template <typename E, typename M>
concept PredicateFor = AnyPredicateFor<E, M> && ORM_QUERY_UNBOUND_PARAMETER<E>;
template <typename E, typename M>
concept PlanPredicateFor = AnyPredicateFor<E, M>;
template <typename E, typename M>
concept ColumnFor = IsTypedColumn<std::remove_cvref_t<E>>::value && ORM_QUERY_MODEL<std::remove_cvref_t<E>, M>;
template <typename M, typename... Columns>
concept ORM_QUERY_MODEL_COLUMNS = (ColumnFor<Columns, M> && ...);
template <typename E>
struct IsTypedAggregatePredicate : std::false_type
{
};
template <typename E>
concept LogicalPredicate =
    PredicateExpression<E> || ((isExpression<E> || IsTypedAggregatePredicate<std::remove_cvref_t<E>>::value) &&
                               std::remove_cvref_t<E>::isAggregatePredicate);
template <auto Member, CollectionOperator Op, typename C>
struct CollectionMeta : ExpressionMeta<typename CollectionTraits<Member>::Model, true, true>
{
    using Child = C;
    using Target = typename CollectionTraits<Member>::Target;
    using Dynamic = TypedPredicate<typename CollectionTraits<Member>::Model, true, true>;
    inline static constexpr auto member = Member;
    inline static constexpr auto operation = Op;
    inline static constexpr bool isPredicate = true;
    inline static constexpr bool staticSqlEligible = std::same_as<C, void> || detail::staticSqlEligible<C>;
    inline static constexpr auto relationName = model::detail::reflectedMemberNameStorage<Member>.view();
    template <typename Children, typename Args>
    static auto render(const Children& children, const Args& args)
    {
        if constexpr (Op == CollectionOperator::Exists)
            return detail::exists(std::string{relationName});
        else if constexpr (Op == CollectionOperator::Any)
            return detail::any(std::string{relationName}, TypedAccess::erase(std::get<0>(children), args));
        else
            return detail::none(std::string{relationName}, TypedAccess::erase(std::get<0>(children), args));
    }
    template <typename Children, typename Args, typename Callback>
    static auto visit(const Children& children, const Args& args, Callback& callback) -> void
    {
        if constexpr (!std::same_as<C, void>)
            visitValues(std::get<0>(children), args, callback);
    }
};
} // namespace orm::query::detail

namespace orm::query
{
template <typename L, typename R>
    requires detail::LogicalPredicate<L> && detail::LogicalPredicate<R> &&
             detail::ORM_QUERY_MODEL<std::remove_cvref_t<R>, typename std::remove_cvref_t<L>::Model> &&
             (std::remove_cvref_t<L>::isAggregatePredicate == std::remove_cvref_t<R>::isAggregatePredicate)
constexpr auto operator&&(const L& left, const R& right)
{
    return detail::makeExpression<ExprKind::Logical, detail::LogicalMeta<L, R, detail::LogicalOperator::And>>(left,
                                                                                                              right);
}
template <typename L, typename R>
    requires detail::LogicalPredicate<L> && detail::LogicalPredicate<R> &&
             detail::ORM_QUERY_MODEL<std::remove_cvref_t<R>, typename std::remove_cvref_t<L>::Model> &&
             (std::remove_cvref_t<L>::isAggregatePredicate == std::remove_cvref_t<R>::isAggregatePredicate)
constexpr auto operator||(const L& left, const R& right)
{
    return detail::makeExpression<ExprKind::Logical, detail::LogicalMeta<L, R, detail::LogicalOperator::Or>>(left,
                                                                                                             right);
}
template <typename E>
    requires detail::LogicalPredicate<E>
constexpr auto operator!(const E& value)
{
    return detail::makeExpression<ExprKind::Not, detail::NotMeta<E>>(value);
}
template <auto Member, typename E>
    requires detail::ORM_QUERY_COLLECTION<Member> &&
             detail::AnyPredicateFor<E, typename detail::CollectionTraits<Member>::Target> &&
             detail::ORM_QUERY_NO_NESTED_COLLECTION<E::containsCollection>
constexpr auto any(const E& predicate)
{
    return detail::makeExpression<ExprKind::Collection,
                                  detail::CollectionMeta<Member, detail::CollectionOperator::Any, E>>(predicate);
}
template <auto Member, typename E>
    requires detail::ORM_QUERY_COLLECTION<Member> &&
             detail::AnyPredicateFor<E, typename detail::CollectionTraits<Member>::Target> &&
             detail::ORM_QUERY_NO_NESTED_COLLECTION<E::containsCollection>
constexpr auto none(const E& predicate)
{
    return detail::makeExpression<ExprKind::Collection,
                                  detail::CollectionMeta<Member, detail::CollectionOperator::None, E>>(predicate);
}
template <auto Member>
    requires detail::ORM_QUERY_COLLECTION<Member>
constexpr auto exists()
{
    return detail::makeExpression<ExprKind::Collection,
                                  detail::CollectionMeta<Member, detail::CollectionOperator::Exists, void>>();
}
template <typename M, typename... P>
    requires(std::same_as<std::remove_cvref_t<P>, QueryParameter> && ...)
auto raw(std::string sql, P... params) -> TypedPredicate<M>
{
    return detail::TypedAccess::make<TypedPredicate<M>>(detail::raw(std::move(sql), std::move(params)...));
}
} // namespace orm::query
