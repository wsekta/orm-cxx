#pragma once

#include <string>
#include <utility>

#include "Predicate.hpp"

namespace orm::query::detail
{
enum class OrderDirection
{
    Asc,
    Desc,
};

/**
 * @brief A single ORDER BY clause.
 */
struct OrderBy
{
    Column column{""};
    OrderDirection direction{OrderDirection::Asc};
    std::string rawSql{};
    bool isRaw{false};
};

/**
 * @brief Creates an ascending ORDER BY clause.
 */
inline auto asc(Column column) -> OrderBy
{
    return OrderBy{.column = std::move(column), .direction = OrderDirection::Asc};
}

/**
 * @brief Creates a descending ORDER BY clause.
 */
inline auto desc(Column column) -> OrderBy
{
    return OrderBy{.column = std::move(column), .direction = OrderDirection::Desc};
}

/**
 * @brief Creates a raw ORDER BY clause.
 */
inline auto rawOrder(std::string sql) -> OrderBy
{
    return OrderBy{.rawSql = std::move(sql), .isRaw = true};
}
} // namespace orm::query::detail

namespace orm::query
{
template <typename Owner>
class TypedOrderBy
{
public:
    using Model = Owner;
    using ParameterTypes = std::tuple<>;
    inline static constexpr auto kind = ExprKind::DynamicOrder;
    inline static constexpr bool isOrder = true;
    inline static constexpr bool staticSqlEligible = false;
    template <typename E>
        requires detail::isExpression<E> && E::isOrder && detail::ORM_QUERY_MODEL<E, Owner> &&
                 detail::ORM_QUERY_UNBOUND_PARAMETER<E>
    TypedOrderBy(const E& expression) : data{detail::erase(expression)}
    {
    }
    template <typename E>
        requires detail::isExpression<E> && E::isOrder && detail::ORM_QUERY_MODEL<E, Owner> &&
                     detail::ORM_QUERY_UNBOUND_PARAMETER<E>
    auto operator=(const E& expression) -> TypedOrderBy&
    {
        data = detail::erase(expression);
        return *this;
    }
    auto dynamic() const -> TypedOrderBy
    {
        return *this;
    }

private:
    friend struct detail::TypedAccess;
    explicit TypedOrderBy(detail::OrderBy value) : data{std::move(value)} {}
    auto runtime() const -> detail::OrderBy
    {
        return data;
    }
    detail::OrderBy data;
};
} // namespace orm::query

namespace orm::query::detail
{
template <typename C, OrderDirection Direction>
struct OrderMeta : ExpressionMeta<typename C::Model>
{
    using Source = C;
    using Dynamic = TypedOrderBy<typename C::Model>;
    inline static constexpr auto direction = Direction;
    inline static constexpr bool isOrder = true;
    inline static constexpr bool staticSqlEligible = true;
    template <typename Children, typename Args>
    static auto render(const Children& children, const Args& args)
    {
        return OrderBy{.column = TypedAccess::erase(std::get<0>(children), args), .direction = Direction};
    }
    template <typename Children, typename Args, typename Callback>
    static auto visit(const Children&, const Args&, Callback&) -> void
    {
    }
};
template <typename E>
struct IsTypedOrderBy : std::false_type
{
};
template <typename M>
struct IsTypedOrderBy<TypedOrderBy<M>> : std::true_type
{
};
template <typename E, typename M>
concept OrderFor =
    (IsTypedOrderBy<std::remove_cvref_t<E>>::value || isExpression<E>) && std::remove_cvref_t<E>::isOrder &&
    ORM_QUERY_MODEL<std::remove_cvref_t<E>, M> && ORM_QUERY_UNBOUND_PARAMETER<E>;
template <typename M, typename... Orders>
concept ORM_QUERY_MODEL_ORDERS = (OrderFor<Orders, M> && ...);
} // namespace orm::query::detail

namespace orm::query
{
template <auto... Members>
constexpr auto asc(TypedColumn<Members...> column)
{
    return detail::makeExpression<ExprKind::Order,
                                  detail::OrderMeta<TypedColumn<Members...>, detail::OrderDirection::Asc>>(column);
}
template <auto... Members>
constexpr auto desc(TypedColumn<Members...> column)
{
    return detail::makeExpression<ExprKind::Order,
                                  detail::OrderMeta<TypedColumn<Members...>, detail::OrderDirection::Desc>>(column);
}
template <typename M>
auto rawOrder(std::string sql) -> TypedOrderBy<M>
{
    return detail::TypedAccess::make<TypedOrderBy<M>>(detail::rawOrder(std::move(sql)));
}
} // namespace orm::query
