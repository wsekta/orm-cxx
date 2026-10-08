#pragma once

#include <string>
#include <utility>
#include <variant>

#include "Aggregate.hpp"

namespace orm::query::detail
{
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

} // namespace orm::query::detail

namespace orm::query
{
template <typename Owner>
class TypedProjection
{
public:
    using Model = Owner;
    using ParameterTypes = std::tuple<>;
    inline static constexpr auto kind = ExprKind::DynamicProjection;
    inline static constexpr bool isProjection = true;
    inline static constexpr bool staticSqlEligible = false;
    template <typename E>
        requires detail::isExpression<E> && E::isProjection && detail::ORM_QUERY_MODEL<E, Owner> &&
                 detail::ORM_QUERY_UNBOUND_PARAMETER<E>
    TypedProjection(const E& expression) : data{detail::erase(expression)}
    {
    }
    template <typename E>
        requires detail::isExpression<E> && E::isProjection && detail::ORM_QUERY_MODEL<E, Owner> &&
                     detail::ORM_QUERY_UNBOUND_PARAMETER<E>
    auto operator=(const E& expression) -> TypedProjection&
    {
        data = detail::erase(expression);
        return *this;
    }
    auto dynamic() const -> TypedProjection
    {
        return *this;
    }

private:
    friend struct detail::TypedAccess;
    explicit TypedProjection(detail::Projection value) : data{std::move(value)} {}
    auto runtime() const -> detail::Projection
    {
        return data;
    }
    detail::Projection data;
};
} // namespace orm::query

namespace orm::query::detail
{
template <typename S, reflection::FixedString Alias>
struct ProjectionMeta : ExpressionMeta<typename S::Model>
{
    using Source = S;
    using Dynamic = TypedProjection<typename S::Model>;
    inline static constexpr auto alias = Alias;
    inline static constexpr bool isProjection = true;
    inline static constexpr bool staticSqlEligible = detail::staticSqlEligible<S>;
    template <typename Children, typename Args>
    static auto render(const Children& children, const Args& args)
    {
        return detail::as(std::string{Alias.view()}, TypedAccess::erase(std::get<0>(children), args));
    }
    template <typename Children, typename Args, typename Callback>
    static auto visit(const Children&, const Args&, Callback&) -> void
    {
    }
};
template <typename S>
struct RuntimeProjectionMeta : ExpressionMeta<typename S::Model>
{
    using Source = S;
    using Dynamic = TypedProjection<typename S::Model>;
    inline static constexpr bool isProjection = true;
    inline static constexpr bool staticSqlEligible = false;
    template <typename Children, typename Args>
    static auto render(const Children& children, const Args& args)
    {
        return detail::as(std::get<1>(children), TypedAccess::erase(std::get<0>(children), args));
    }
    template <typename Children, typename Args, typename Callback>
    static auto visit(const Children&, const Args&, Callback&) -> void
    {
    }
};
template <typename E>
struct IsTypedProjection : std::false_type
{
};
template <typename M>
struct IsTypedProjection<TypedProjection<M>> : std::true_type
{
};
template <typename E, typename M>
concept ProjectionFor =
    (IsTypedProjection<std::remove_cvref_t<E>>::value || isExpression<E>) && std::remove_cvref_t<E>::isProjection &&
    ORM_QUERY_MODEL<std::remove_cvref_t<E>, M> && ORM_QUERY_UNBOUND_PARAMETER<E>;
template <typename M, typename... Projections>
concept ORM_QUERY_MODEL_PROJECTIONS = (ProjectionFor<Projections, M> && ...);
template <typename S>
concept ProjectionSourceExpression = IsTypedColumn<std::remove_cvref_t<S>>::value || AggregateExpressionType<S>;
} // namespace orm::query::detail

namespace orm::query
{
template <typename S>
    requires detail::ProjectionSourceExpression<S>
constexpr auto as(std::string alias, S source)
{
    return detail::makeExpression<ExprKind::Projection, detail::RuntimeProjectionMeta<S>>(source, std::move(alias));
}
template <reflection::FixedString Alias, typename S>
    requires detail::ProjectionSourceExpression<S>
constexpr auto as(S source)
{
    return detail::makeExpression<ExprKind::Projection, detail::ProjectionMeta<S, Alias>>(source);
}
} // namespace orm::query
