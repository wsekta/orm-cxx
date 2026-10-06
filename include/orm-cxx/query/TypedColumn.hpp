#pragma once

#include <concepts>
#include <limits>
#include <string_view>
#include <type_traits>

#include "orm-cxx/model/StaticModel.hpp"
#include "Predicate.hpp"

namespace orm::query::detail
{
struct TypedAccess
{
    template <typename T, typename... Args>
    static auto make(Args&&... args) -> T
    {
        return T(std::forward<Args>(args)...);
    }
    template <typename T>
    static auto erase(const T& value)
    {
        return value.runtime();
    }
};
template <typename T>
auto erase(const T& value)
{
    return TypedAccess::erase(value);
}

template <typename T>
using scalar_t = std::remove_cv_t<model::detail::static_optional_value_t<std::remove_cvref_t<T>>>;
template <typename T>
inline constexpr bool numericScalar =
    std::is_arithmetic_v<std::remove_cvref_t<T>> && !std::same_as<std::remove_cvref_t<T>, bool> &&
    requires { model::LogicalTypeTraits<std::remove_cvref_t<T>>::value; };
template <typename T>
inline constexpr bool supportedScalar =
    numericScalar<T> || std::same_as<std::remove_cvref_t<T>, bool> || std::same_as<std::remove_cvref_t<T>, std::string>;

template <typename Target, typename Source>
inline constexpr bool isSafeNumericWidening = []
{
    using T = std::remove_cvref_t<Target>;
    using S = std::remove_cvref_t<Source>;
    if constexpr (!numericScalar<T> || !numericScalar<S>)
        return false;
    else if constexpr (std::is_integral_v<T> && std::is_integral_v<S>)
        return (!std::is_signed_v<S> || std::is_signed_v<T>) &&
               std::numeric_limits<T>::digits >= std::numeric_limits<S>::digits;
    else if constexpr (std::is_floating_point_v<T> && std::is_integral_v<S>)
        return std::numeric_limits<T>::radix == 2 && std::numeric_limits<T>::digits >= std::numeric_limits<S>::digits &&
               std::numeric_limits<T>::max_exponent > std::numeric_limits<S>::digits;
    else if constexpr (std::is_floating_point_v<T> && std::is_floating_point_v<S>)
        return std::numeric_limits<T>::radix == std::numeric_limits<S>::radix &&
               std::numeric_limits<T>::digits >= std::numeric_limits<S>::digits &&
               std::numeric_limits<T>::max_exponent >= std::numeric_limits<S>::max_exponent &&
               std::numeric_limits<T>::min_exponent <= std::numeric_limits<S>::min_exponent;
    else
        return false;
}();

template <typename T>
inline constexpr bool stringValue =
    std::same_as<std::remove_cvref_t<T>, std::string> || std::same_as<std::remove_cvref_t<T>, std::string_view> ||
    std::same_as<std::decay_t<T>, const char*> || std::same_as<std::decay_t<T>, char*>;
template <typename Target, typename Source>
inline constexpr bool compatibleValue =
    isSafeNumericWidening<Target, Source> ||
    (std::same_as<Target, bool> && std::same_as<std::remove_cvref_t<Source>, bool>) ||
    (std::same_as<Target, std::string> && stringValue<Source>);
template <typename Target, typename Source>
concept ORM_QUERY_VALUE = compatibleValue<Target, Source>;
template <typename Model, typename Expected>
concept ORM_QUERY_MODEL_TYPE = std::same_as<Model, Expected>;
template <typename E, typename M>
concept ORM_QUERY_MODEL = requires { typename E::Model; } && ORM_QUERY_MODEL_TYPE<typename E::Model, M>;
template <auto... Members>
concept ORM_QUERY_MEMBER = sizeof...(Members) > 0 && (std::is_member_object_pointer_v<decltype(Members)> && ...) &&
                           ((Members != nullptr) && ...);
template <typename T>
concept ORM_QUERY_NUMERIC = numericScalar<T>;
template <typename T>
concept ORM_QUERY_STRING = std::same_as<T, std::string>;
template <typename T>
concept ORM_QUERY_ORDERABLE = numericScalar<T> || std::same_as<T, std::string>;
template <bool Nullable>
concept ORM_QUERY_NULLABLE = Nullable;

template <typename Target, typename Source>
    requires ORM_QUERY_VALUE<Target, Source>
auto typedValue(Source&& value) -> QueryValue
{
    if constexpr (std::same_as<Target, std::string>)
    {
        if constexpr (std::is_pointer_v<std::decay_t<Source>>)
            if (value == nullptr)
                throw std::invalid_argument{"A string query value must not be a null pointer"};
        return QueryValue{std::string{std::forward<Source>(value)}};
    }
    else
        return QueryValue{static_cast<Target>(value)};
}

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
    static auto path() -> std::string
    {
        return std::string{model::detail::reflectedMemberNameStorage<Member>.view()};
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
    static auto path() -> std::string
    {
        return std::string{model::detail::reflectedMemberNameStorage<Relation>.view()} + "." + Terminal::path();
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
    inline static constexpr bool isPredicate = true;
    inline static constexpr bool writeSafe = WriteSafe;
    inline static constexpr bool containsCollection = ContainsCollection;

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
    inline static constexpr bool isColumn = true;
    inline static constexpr bool nullable = Traits::nullable;
    inline static constexpr bool writeSafe = Traits::writeSafe;
    using Result = TypedPredicate<Model, writeSafe>;
    template <typename T>
        requires detail::ORM_QUERY_VALUE<Value, T>
    auto operator==(T&& v) const -> Result
    {
        return compare(detail::ComparisonOperator::Equal, std::forward<T>(v));
    }
    template <typename T>
        requires detail::ORM_QUERY_VALUE<Value, T>
    auto operator!=(T&& v) const -> Result
    {
        return compare(detail::ComparisonOperator::NotEqual, std::forward<T>(v));
    }
    template <typename T>
        requires detail::ORM_QUERY_VALUE<Value, T> && detail::ORM_QUERY_ORDERABLE<Value>
    auto operator>(T&& v) const -> Result
    {
        return compare(detail::ComparisonOperator::Greater, std::forward<T>(v));
    }
    template <typename T>
        requires detail::ORM_QUERY_VALUE<Value, T> && detail::ORM_QUERY_ORDERABLE<Value>
    auto operator>=(T&& v) const -> Result
    {
        return compare(detail::ComparisonOperator::GreaterOrEqual, std::forward<T>(v));
    }
    template <typename T>
        requires detail::ORM_QUERY_VALUE<Value, T> && detail::ORM_QUERY_ORDERABLE<Value>
    auto operator<(T&& v) const -> Result
    {
        return compare(detail::ComparisonOperator::Less, std::forward<T>(v));
    }
    template <typename T>
        requires detail::ORM_QUERY_VALUE<Value, T> && detail::ORM_QUERY_ORDERABLE<Value>
    auto operator<=(T&& v) const -> Result
    {
        return compare(detail::ComparisonOperator::LessOrEqual, std::forward<T>(v));
    }
    template <typename T>
        requires detail::ORM_QUERY_VALUE<Value, T> && detail::ORM_QUERY_STRING<Value>
    auto like(T&& v) const -> Result
    {
        return compare(detail::ComparisonOperator::Like, std::forward<T>(v));
    }
    template <typename T>
        requires detail::ORM_QUERY_VALUE<Value, T> && detail::ORM_QUERY_STRING<Value>
    auto notLike(T&& v) const -> Result
    {
        return compare(detail::ComparisonOperator::NotLike, std::forward<T>(v));
    }
    auto isNull() const -> Result
        requires detail::ORM_QUERY_NULLABLE<nullable>
    {
        return detail::TypedAccess::make<Result>(runtime().isNull());
    }
    auto isNotNull() const -> Result
        requires detail::ORM_QUERY_NULLABLE<nullable>
    {
        return detail::TypedAccess::make<Result>(runtime().isNotNull());
    }
    auto operator==(std::nullptr_t) const -> Result
        requires detail::ORM_QUERY_NULLABLE<nullable>
    {
        return isNull();
    }
    auto operator!=(std::nullptr_t) const -> Result
        requires detail::ORM_QUERY_NULLABLE<nullable>
    {
        return isNotNull();
    }
    auto operator==(std::nullopt_t) const -> Result
        requires detail::ORM_QUERY_NULLABLE<nullable>
    {
        return isNull();
    }
    auto operator!=(std::nullopt_t) const -> Result
        requires detail::ORM_QUERY_NULLABLE<nullable>
    {
        return isNotNull();
    }
    template <typename T>
        requires detail::ORM_QUERY_VALUE<Value, T>
    auto in(std::initializer_list<T> v) const -> Result
    {
        return list(detail::ListOperator::In, v);
    }
    template <typename T>
        requires detail::ORM_QUERY_VALUE<Value, T>
    auto in(const std::vector<T>& v) const -> Result
    {
        return list(detail::ListOperator::In, v);
    }
    template <typename T>
        requires detail::ORM_QUERY_VALUE<Value, T>
    auto notIn(std::initializer_list<T> v) const -> Result
    {
        return list(detail::ListOperator::NotIn, v);
    }
    template <typename T>
        requires detail::ORM_QUERY_VALUE<Value, T>
    auto notIn(const std::vector<T>& v) const -> Result
    {
        return list(detail::ListOperator::NotIn, v);
    }
    template <typename L, typename U>
        requires detail::ORM_QUERY_VALUE<Value, L> && detail::ORM_QUERY_VALUE<Value, U> &&
                     detail::ORM_QUERY_ORDERABLE<Value>
    auto between(L&& l, U&& u) const -> Result
    {
        return range(detail::BetweenOperator::Between, std::forward<L>(l), std::forward<U>(u));
    }
    template <typename L, typename U>
        requires detail::ORM_QUERY_VALUE<Value, L> && detail::ORM_QUERY_VALUE<Value, U> &&
                     detail::ORM_QUERY_ORDERABLE<Value>
    auto notBetween(L&& l, U&& u) const -> Result
    {
        return range(detail::BetweenOperator::NotBetween, std::forward<L>(l), std::forward<U>(u));
    }

private:
    friend struct detail::TypedAccess;
    TypedColumn() = default;
    auto runtime() const -> detail::Column
    {
        return detail::col(Traits::path());
    }
    template <typename T>
    auto compare(detail::ComparisonOperator op, T&& v) const -> Result
    {
        return detail::TypedAccess::make<Result>(detail::Predicate{detail::PredicateNode{
            detail::ComparisonExpression{runtime(), op, detail::typedValue<Value>(std::forward<T>(v))}}});
    }
    template <typename Container>
    auto list(detail::ListOperator op, const Container& values) const -> Result
    {
        if (values.size() == 0)
            throw std::invalid_argument{"IN predicate requires at least one value"};
        std::vector<QueryValue> bound;
        bound.reserve(values.size());
        for (const auto& v : values)
            bound.push_back(detail::typedValue<Value>(static_cast<typename Container::value_type>(v)));
        return detail::TypedAccess::make<Result>(
            detail::Predicate{detail::PredicateNode{detail::ListExpression{runtime(), op, std::move(bound)}}});
    }
    template <typename L, typename U>
    auto range(detail::BetweenOperator op, L&& l, U&& u) const -> Result
    {
        return detail::TypedAccess::make<Result>(detail::Predicate{detail::PredicateNode{
            detail::BetweenExpression{runtime(), op, detail::typedValue<Value>(std::forward<L>(l)),
                                      detail::typedValue<Value>(std::forward<U>(u))}}});
    }
};

template <auto... Members>
    requires detail::ORM_QUERY_MEMBER<Members...>
auto col() -> TypedColumn<Members...>
{
    return detail::TypedAccess::make<TypedColumn<Members...>>();
}

template <typename L, bool LW, bool LC, typename R, bool RW, bool RC>
    requires detail::ORM_QUERY_MODEL<TypedPredicate<R, RW, RC>, L>
auto operator&&(const TypedPredicate<L, LW, LC>& l,
                const TypedPredicate<R, RW, RC>& r) -> TypedPredicate<L, LW && RW, LC || RC>
{
    return detail::TypedAccess::make<TypedPredicate<L, LW && RW, LC || RC>>(detail::erase(l) && detail::erase(r));
}
template <typename L, bool LW, bool LC, typename R, bool RW, bool RC>
    requires detail::ORM_QUERY_MODEL<TypedPredicate<R, RW, RC>, L>
auto operator||(const TypedPredicate<L, LW, LC>& l,
                const TypedPredicate<R, RW, RC>& r) -> TypedPredicate<L, LW && RW, LC || RC>
{
    return detail::TypedAccess::make<TypedPredicate<L, LW && RW, LC || RC>>(detail::erase(l) || detail::erase(r));
}
template <typename M, bool W, bool C>
auto operator!(const TypedPredicate<M, W, C>& v) -> TypedPredicate<M, W, C>
{
    return detail::TypedAccess::make<TypedPredicate<M, W, C>>(!detail::erase(v));
}

template <auto Member, typename M, bool W, bool C>
    requires detail::ORM_QUERY_COLLECTION<Member> &&
                 detail::ORM_QUERY_MODEL_TYPE<M, typename detail::CollectionTraits<Member>::Target> &&
                 detail::ORM_QUERY_NO_NESTED_COLLECTION<C>
auto any(const TypedPredicate<M, W, C>& v)
    -> TypedPredicate<typename detail::CollectionTraits<Member>::Model, true, true>
{
    using R = TypedPredicate<typename detail::CollectionTraits<Member>::Model, true, true>;
    return detail::TypedAccess::make<R>(detail::any(detail::CollectionTraits<Member>::name(), detail::erase(v)));
}
template <auto Member, typename M, bool W, bool C>
    requires detail::ORM_QUERY_COLLECTION<Member> &&
                 detail::ORM_QUERY_MODEL_TYPE<M, typename detail::CollectionTraits<Member>::Target> &&
                 detail::ORM_QUERY_NO_NESTED_COLLECTION<C>
auto none(const TypedPredicate<M, W, C>& v)
    -> TypedPredicate<typename detail::CollectionTraits<Member>::Model, true, true>
{
    using R = TypedPredicate<typename detail::CollectionTraits<Member>::Model, true, true>;
    return detail::TypedAccess::make<R>(detail::none(detail::CollectionTraits<Member>::name(), detail::erase(v)));
}
template <auto Member>
    requires detail::ORM_QUERY_COLLECTION<Member>
auto exists() -> TypedPredicate<typename detail::CollectionTraits<Member>::Model, true, true>
{
    using R = TypedPredicate<typename detail::CollectionTraits<Member>::Model, true, true>;
    return detail::TypedAccess::make<R>(detail::exists(detail::CollectionTraits<Member>::name()));
}
template <typename M, typename... P>
    requires(std::same_as<std::remove_cvref_t<P>, QueryParameter> && ...)
auto raw(std::string sql, P... params) -> TypedPredicate<M>
{
    return detail::TypedAccess::make<TypedPredicate<M>>(detail::raw(std::move(sql), std::move(params)...));
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
template <typename E, typename M>
concept PredicateFor = IsTypedPredicate<std::remove_cvref_t<E>>::value && ORM_QUERY_MODEL<E, M>;
template <typename E, typename M>
concept ColumnFor = IsTypedColumn<std::remove_cvref_t<E>>::value && ORM_QUERY_MODEL<E, M>;
} // namespace orm::query::detail
