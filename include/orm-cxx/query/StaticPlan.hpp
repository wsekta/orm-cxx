#pragma once

#include <cstddef>
#include <limits>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

#include "orm-cxx/projection_query.hpp"
#include "orm-cxx/query.hpp"
#include "orm-cxx/update.hpp"

namespace orm::query::detail
{
enum class PlanOperation
{
    Select,
    Update,
    Remove
};
struct NoClause
{
};
template <auto Member>
struct IncludeTag
{
    inline static constexpr auto member = Member;
};

struct PlanAccess
{
    template <typename P, typename S>
    static constexpr auto make(S&& state) -> P
    {
        return P{std::forward<S>(state)};
    }
    template <typename P>
    static constexpr auto get(const P& plan) -> const typename P::State&
    {
        return plan.state;
    }
};

template <typename Column, typename ValueStorage>
struct Assignment
{
    using Source = Column;
    using Stored = ValueStorage;
    using ParameterTypes = detail::ParameterTypes<ValueStorage>;
    ValueStorage value;
};

template <typename T>
inline constexpr bool isStaticPlan = false;
template <typename C, typename V>
inline constexpr bool setCompatible = []
{
    using T = parameter_value_t<V>;
    if constexpr (std::same_as<T, std::nullopt_t> || std::same_as<T, std::nullptr_t>)
        return C::nullable;
    else if constexpr (model::isNullable<T>)
        return C::nullable && compatibleValue<typename C::Value, scalar_t<T>>;
    else
        return compatibleValue<typename C::Value, T>;
}();
template <typename C, typename V>
concept ORM_QUERY_SET_VALUE = setCompatible<C, V>;

template <typename C, typename V, typename Args>
auto applyAssignment(orm::Update<typename C::Model>& result, const Assignment<C, V>& assignment,
                     const Args& args) -> void
{
    auto value = resolveValue(assignment.value, args);
    if constexpr (std::same_as<decltype(value), std::nullptr_t>)
        result.set(TypedAccess::make<C>(), std::nullopt);
    else
        result.set(TypedAccess::make<C>(), std::move(value));
}
} // namespace orm::query::detail

namespace orm::query
{
/** An immutable, reusable query shape. Values are owned and bound at execution. */
template <detail::PlanOperation Operation, typename Owner, typename ResultType, typename StateTuple,
          bool Distinct = false, bool Join = true>
class StaticPlan
{
public:
    using Model = Owner;
    using Result = ResultType;
    using State = StateTuple;
    using ParameterTypes = detail::ParameterTypes<State>;
    using Predicate = std::tuple_element_t<0, State>;
    using Orders = std::tuple_element_t<1, State>;
    using Groups = std::tuple_element_t<2, State>;
    using Having = std::tuple_element_t<3, State>;
    using Projections = std::tuple_element_t<4, State>;
    using Assignments = std::tuple_element_t<5, State>;
    using Limit = std::tuple_element_t<6, State>;
    using Offset = std::tuple_element_t<7, State>;
    using Includes = std::tuple_element_t<8, State>;
    inline static constexpr auto operation = Operation;
    inline static constexpr bool isDistinct = Distinct;
    inline static constexpr bool shouldJoin = Join;
    inline static constexpr bool isProjection = !std::same_as<Projections, std::tuple<>>;
    inline static constexpr bool hasPredicate = !std::same_as<Predicate, detail::NoClause>;
    inline static constexpr bool hasHaving = !std::same_as<Having, detail::NoClause>;
    inline static constexpr bool hasLimit = !std::same_as<Limit, detail::NoClause>;
    inline static constexpr bool hasOffset = !std::same_as<Offset, detail::NoClause>;

    template <typename P>
        requires detail::PlanPredicateFor<P, Model> &&
                 (Operation == detail::PlanOperation::Select || detail::ORM_QUERY_WRITE_SAFE<P>)
    constexpr auto where(P predicate) const
    {
        return replace<0>(std::move(predicate));
    }
    template <typename P>
        requires detail::PlanPredicateFor<P, Model> &&
                 (Operation == detail::PlanOperation::Select || detail::ORM_QUERY_WRITE_SAFE<P>)
    constexpr auto andWhere(P predicate) const
    {
        if constexpr (hasPredicate)
            return where(std::get<0>(state) && std::move(predicate));
        else
            return where(std::move(predicate));
    }
    template <typename P>
        requires detail::PlanPredicateFor<P, Model> &&
                 (Operation == detail::PlanOperation::Select || detail::ORM_QUERY_WRITE_SAFE<P>)
    constexpr auto orWhere(P predicate) const
    {
        if constexpr (hasPredicate)
            return where(std::get<0>(state) || std::move(predicate));
        else
            return where(std::move(predicate));
    }
    template <typename... O>
        requires(Operation == detail::PlanOperation::Select) && detail::ORM_QUERY_MODEL_ORDERS<Model, O...>
    constexpr auto orderBy(O... orders) const
    {
        return replace<1>(std::tuple{std::move(orders)...});
    }
    template <typename... C>
        requires(Operation == detail::PlanOperation::Select) && detail::ORM_QUERY_MODEL_COLUMNS<Model, C...>
    constexpr auto groupBy(C... columns) const
    {
        return replace<2>(std::tuple{std::move(columns)...});
    }
    template <typename P>
        requires(Operation == detail::PlanOperation::Select) && detail::PlanAggregatePredicateFor<P, Model>
    constexpr auto having(P predicate) const
    {
        return replace<3>(std::move(predicate));
    }
    template <typename P>
        requires(Operation == detail::PlanOperation::Select) && detail::PlanAggregatePredicateFor<P, Model>
    constexpr auto andHaving(P predicate) const
    {
        if constexpr (hasHaving)
            return having(std::get<3>(state) && std::move(predicate));
        else
            return having(std::move(predicate));
    }
    template <typename P>
        requires(Operation == detail::PlanOperation::Select) && detail::PlanAggregatePredicateFor<P, Model>
    constexpr auto orHaving(P predicate) const
    {
        if constexpr (hasHaving)
            return having(std::get<3>(state) || std::move(predicate));
        else
            return having(std::move(predicate));
    }
    template <typename... P>
        requires(Operation == detail::PlanOperation::Select) && isProjection &&
                detail::ORM_QUERY_MODEL_PROJECTIONS<Model, P...>
    constexpr auto project(P... projections) const
    {
        static_assert(sizeof...(P) > 0, "ORM_QUERY_PROJECTION: a projection plan requires projected fields");
        return replace<4>(std::tuple{std::move(projections)...});
    }
    constexpr auto distinct() const
        requires(Operation == detail::PlanOperation::Select)
    {
        return detail::PlanAccess::make<StaticPlan<Operation, Model, Result, State, true, Join>>(state);
    }
    constexpr auto disableJoining() const
        requires(Operation == detail::PlanOperation::Select)
    {
        return detail::PlanAccess::make<StaticPlan<Operation, Model, Result, State, Distinct, false>>(state);
    }
    constexpr auto limit(std::size_t value) const
        requires(Operation == detail::PlanOperation::Select)
    {
        return replace<6>(value);
    }
    constexpr auto offset(std::size_t value) const
        requires(Operation == detail::PlanOperation::Select)
    {
        return replace<7>(value);
    }
    template <std::size_t I>
    constexpr auto limit(Parameter<std::size_t, I> value) const
        requires(Operation == detail::PlanOperation::Select)
    {
        return replace<6>(value);
    }
    template <std::size_t I>
    constexpr auto offset(Parameter<std::size_t, I> value) const
        requires(Operation == detail::PlanOperation::Select)
    {
        return replace<7>(value);
    }
    template <auto Member>
        requires(Operation == detail::PlanOperation::Select) &&
                (!isProjection) && detail::ORM_QUERY_COLLECTION<Member> &&
                detail::ORM_QUERY_MODEL_TYPE<typename detail::CollectionTraits<Member>::Model, Model>
    constexpr auto include() const
    {
        constexpr bool alreadyIncluded = []<std::size_t... I>(std::index_sequence<I...>) {
            return (std::same_as<std::tuple_element_t<I, Includes>, detail::IncludeTag<Member>> || ...);
        }(std::make_index_sequence<std::tuple_size_v<Includes>>{});
        if constexpr (alreadyIncluded)
            return *this;
        else
            return replace<8>(std::tuple_cat(std::get<8>(state), std::tuple{detail::IncludeTag<Member>{}}));
    }
    template <auto... Members, typename V>
        requires(Operation == detail::PlanOperation::Update) && detail::ColumnFor<TypedColumn<Members...>, Model> &&
                detail::ORM_QUERY_WRITE_SAFE<TypedColumn<Members...>> &&
                detail::ORM_QUERY_SET_VALUE<TypedColumn<Members...>, V>
    constexpr auto set(TypedColumn<Members...>, V&& value) const
    {
        auto stored = detail::captureValue(std::forward<V>(value));
        using A = detail::Assignment<TypedColumn<Members...>, decltype(stored)>;
        return replace<5>(std::tuple_cat(std::get<5>(state), std::tuple{A{std::move(stored)}}));
    }
    static consteval auto validateShape() -> void
    {
        if constexpr (Operation != detail::PlanOperation::Select)
            static_assert(hasPredicate, "ORM_QUERY_REQUIRED_WHERE: a mutation plan requires WHERE");
        if constexpr (Operation == detail::PlanOperation::Update)
            static_assert(std::tuple_size_v<Assignments> != 0,
                          "ORM_QUERY_UPDATE_ASSIGNMENTS: an update plan requires SET");
    }
    template <typename... Args>
    auto toDynamic(Args&&... values) const
    {
        validateShape();
        detail::validateParameters<StaticPlan, Args...>();
        const auto args = std::forward_as_tuple(values...);
        if constexpr (Operation == detail::PlanOperation::Remove)
            return detail::bindExpression(std::get<0>(state), args);
        else if constexpr (Operation == detail::PlanOperation::Update)
        {
            orm::Update<Model> result;
            std::apply([&](const auto&... assignment) { (detail::applyAssignment(result, assignment, args), ...); },
                       std::get<5>(state));
            result.where(detail::bindExpression(std::get<0>(state), args));
            return result;
        }
        else
        {
            auto result = [&]
            {
                if constexpr (isProjection)
                {
                    orm::ProjectionQuery<Model, Result> output;
                    std::apply([&](const auto&... projection) { output.project(projection...); }, std::get<4>(state));
                    return output;
                }
                else
                    return orm::Query<Model>{};
            }();
            if constexpr (hasPredicate)
                result.where(detail::bindExpression(std::get<0>(state), args));
            std::apply([&](const auto&... order) { result.orderBy(order...); }, std::get<1>(state));
            std::apply([&](const auto&... group) { result.groupBy(group...); }, std::get<2>(state));
            if constexpr (hasHaving)
                result.having(detail::bindExpression(std::get<3>(state), args));
            if constexpr (Distinct)
                result.distinct();
            if constexpr (!Join)
                result.disableJoining();
            if constexpr (hasLimit)
                result.limit(paginationValue(std::get<6>(state), args));
            if constexpr (hasOffset)
                result.offset(paginationValue(std::get<7>(state), args));
            if constexpr (!isProjection)
                std::apply([&](const auto&... include)
                           { (result.template include<std::remove_cvref_t<decltype(include)>::member>(), ...); },
                           std::get<8>(state));
            return result;
        }
    }

private:
    friend struct detail::PlanAccess;
    constexpr explicit StaticPlan(State data) : state{std::move(data)} {}
    template <std::size_t Index, typename V>
    constexpr auto replace(V value) const
    {
        auto newState = [&]<std::size_t... I>(std::index_sequence<I...>)
        {
            auto element = [&]<std::size_t N>()
            {
                if constexpr (N == Index)
                    return value;
                else
                    return std::get<N>(state);
            };
            return std::tuple { element.template operator()<I>()... };
        }(std::make_index_sequence<std::tuple_size_v<State>>{});
        return detail::PlanAccess::make<StaticPlan<Operation, Model, Result, decltype(newState), Distinct, Join>>(
            std::move(newState));
    }
    template <typename V, typename A>
    static auto paginationValue(const V& value, const A& args) -> std::size_t
    {
        const auto result = detail::resolveValue(value, args);
        if (result > static_cast<std::size_t>(std::numeric_limits<long long>::max()))
            throw std::out_of_range{"Pagination exceeds the supported signed 64-bit range"};
        return result;
    }
    State state;
};

namespace detail
{
using EmptyPlanState = std::tuple<NoClause, std::tuple<>, std::tuple<>, NoClause, std::tuple<>, std::tuple<>, NoClause,
                                  NoClause, std::tuple<>>;
template <PlanOperation O, typename M, typename R, typename S, bool D, bool J>
inline constexpr bool isStaticPlan<StaticPlan<O, M, R, S, D, J>> = true;
template <typename P>
concept StaticPlanType = isStaticPlan<std::remove_cvref_t<P>>;
} // namespace detail

template <typename Model>
constexpr auto select()
{
    return detail::PlanAccess::make<StaticPlan<detail::PlanOperation::Select, Model, Model, detail::EmptyPlanState>>(
        detail::EmptyPlanState{});
}
template <typename Source, typename Result, typename... P>
    requires detail::ORM_QUERY_MODEL_PROJECTIONS<Source, P...>
constexpr auto selectAs(P... projections)
{
    static_assert(sizeof...(P) > 0, "ORM_QUERY_PROJECTION: a projection plan requires projected fields");
    using State = std::tuple<detail::NoClause, std::tuple<>, std::tuple<>, detail::NoClause, std::tuple<P...>,
                             std::tuple<>, detail::NoClause, detail::NoClause, std::tuple<>>;
    return detail::PlanAccess::make<StaticPlan<detail::PlanOperation::Select, Source, Result, State>>(
        State{{}, {}, {}, {}, std::tuple{std::move(projections)...}, {}, {}, {}, {}});
}
template <typename Model>
constexpr auto update()
{
    return detail::PlanAccess::make<StaticPlan<detail::PlanOperation::Update, Model, Model, detail::EmptyPlanState>>(
        detail::EmptyPlanState{});
}
template <typename Model>
constexpr auto remove()
{
    return detail::PlanAccess::make<StaticPlan<detail::PlanOperation::Remove, Model, Model, detail::EmptyPlanState>>(
        detail::EmptyPlanState{});
}
} // namespace orm::query
