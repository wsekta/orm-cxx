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
    inline static constexpr bool isOrder = true;

private:
    friend struct detail::TypedAccess;
    explicit TypedOrderBy(detail::OrderBy value) : data{std::move(value)} {}
    auto runtime() const -> detail::OrderBy
    {
        return data;
    }
    detail::OrderBy data;
};
template <auto... Members>
auto asc(TypedColumn<Members...> column) -> TypedOrderBy<typename TypedColumn<Members...>::Model>
{
    using R = TypedOrderBy<typename TypedColumn<Members...>::Model>;
    return detail::TypedAccess::make<R>(detail::asc(detail::erase(column)));
}
template <auto... Members>
auto desc(TypedColumn<Members...> column) -> TypedOrderBy<typename TypedColumn<Members...>::Model>
{
    using R = TypedOrderBy<typename TypedColumn<Members...>::Model>;
    return detail::TypedAccess::make<R>(detail::desc(detail::erase(column)));
}
template <typename Model>
auto rawOrder(std::string sql) -> TypedOrderBy<Model>
{
    return detail::TypedAccess::make<TypedOrderBy<Model>>(detail::rawOrder(std::move(sql)));
}
} // namespace orm::query
namespace orm::query::detail
{
template <typename E>
struct IsTypedOrderBy : std::false_type
{
};
template <typename M>
struct IsTypedOrderBy<TypedOrderBy<M>> : std::true_type
{
};
template <typename E, typename M>
concept OrderFor = IsTypedOrderBy<std::remove_cvref_t<E>>::value && ORM_QUERY_MODEL<E, M>;
template <typename M, typename... Orders>
concept ORM_QUERY_MODEL_ORDERS = (OrderFor<Orders, M> && ...);
} // namespace orm::query::detail
