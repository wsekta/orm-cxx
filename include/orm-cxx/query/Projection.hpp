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
    inline static constexpr bool isProjection = true;

private:
    friend struct detail::TypedAccess;
    explicit TypedProjection(detail::Projection value) : data{std::move(value)} {}
    auto runtime() const -> detail::Projection
    {
        return data;
    }
    detail::Projection data;
};
template <auto... Members>
auto as(std::string alias, TypedColumn<Members...> column) -> TypedProjection<typename TypedColumn<Members...>::Model>
{
    using R = TypedProjection<typename TypedColumn<Members...>::Model>;
    return detail::TypedAccess::make<R>(detail::as(std::move(alias), detail::erase(column)));
}
template <typename M, typename V, bool N>
auto as(std::string alias, TypedAggregate<M, V, N> aggregate) -> TypedProjection<M>
{
    return detail::TypedAccess::make<TypedProjection<M>>(detail::as(std::move(alias), detail::erase(aggregate)));
}
} // namespace orm::query
namespace orm::query::detail
{
template <typename E>
struct IsTypedProjection : std::false_type
{
};
template <typename M>
struct IsTypedProjection<TypedProjection<M>> : std::true_type
{
};
template <typename E, typename M>
concept ProjectionFor = IsTypedProjection<std::remove_cvref_t<E>>::value && ORM_QUERY_MODEL<E, M>;
} // namespace orm::query::detail
