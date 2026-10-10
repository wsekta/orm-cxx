module;

#include <cstddef>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

export module orm:query_projection;

import orm.reflection;
import :foundation;
import :model_mapping;
import :model_columns;
import :query_value;
import :query_runtime_predicate;
import :query_parameters;
import :query_expression;
import :query_columns;
import :query_aggregate;

// query/OrderBy.hpp
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
export
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
}
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
export
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
}
} // namespace orm::query

// query/Projection.hpp
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
export
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
}
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
export
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
}
} // namespace orm::query

// query/SelectSpec.hpp
namespace orm::query::detail
{
/**
 * @brief Runtime SELECT options independent from static model metadata.
 */
struct SelectSpec
{
    std::optional<std::size_t> offset = std::nullopt;
    std::optional<std::size_t> limit = std::nullopt;
    std::optional<Predicate> predicate = std::nullopt;
    std::vector<OrderBy> orderBy;
    std::vector<Projection> projections;
    std::vector<Column> groupBy;
    std::optional<AggregatePredicate> having = std::nullopt;
    std::vector<std::string> includes;
    bool isDistinct = false;
    bool shouldJoin = true;
};
} // namespace orm::query::detail

// query/UpdateSpec.hpp
namespace orm::query::detail
{
struct UpdateValue
{
    std::optional<QueryValue> value;
};

struct UpdateAssignment
{
    Column column;
    UpdateValue value;
};

/**
 * @brief Runtime UPDATE options independent from static model metadata.
 */
struct UpdateSpec
{
    std::vector<UpdateAssignment> assignments;
    std::optional<Predicate> predicate = std::nullopt;
};
} // namespace orm::query::detail
