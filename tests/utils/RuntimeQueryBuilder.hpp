#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

namespace orm::tests
{
/** Test-only builders for exercising malformed renderer input independently of the public DSL. */
template <typename Model>
class RuntimeQuery
{
public:
    auto where(const query::detail::Predicate& predicate) -> RuntimeQuery&
    {
        data.predicate = predicate;
        return *this;
    }

    auto andWhere(const query::detail::Predicate& predicate) -> RuntimeQuery&
    {
        data.predicate = data.predicate.has_value() ? data.predicate.value() && predicate : predicate;
        return *this;
    }

    auto orWhere(const query::detail::Predicate& predicate) -> RuntimeQuery&
    {
        data.predicate = data.predicate.has_value() ? data.predicate.value() || predicate : predicate;
        return *this;
    }

    template <typename... Orders>
    auto orderBy(Orders... orders) -> RuntimeQuery&
    {
        data.orderBy = {std::move(orders)...};
        return *this;
    }

    template <typename... Columns>
    auto groupBy(Columns... columns) -> RuntimeQuery&
    {
        data.groupBy = {std::move(columns)...};
        return *this;
    }

    auto having(const query::detail::AggregatePredicate& predicate) -> RuntimeQuery&
    {
        data.having = predicate;
        return *this;
    }

    auto andHaving(const query::detail::AggregatePredicate& predicate) -> RuntimeQuery&
    {
        data.having = data.having.has_value() ? data.having.value() && predicate : predicate;
        return *this;
    }

    auto orHaving(const query::detail::AggregatePredicate& predicate) -> RuntimeQuery&
    {
        data.having = data.having.has_value() ? data.having.value() || predicate : predicate;
        return *this;
    }

    auto distinct() -> RuntimeQuery&
    {
        data.isDistinct = true;
        return *this;
    }

    auto limit(std::size_t value) -> RuntimeQuery&
    {
        data.limit = value;
        return *this;
    }

    auto offset(std::size_t value) -> RuntimeQuery&
    {
        data.offset = value;
        return *this;
    }

    auto disableJoining() -> RuntimeQuery&
    {
        data.shouldJoin = false;
        return *this;
    }

    [[nodiscard]] auto getData() const -> const query::detail::SelectSpec&
    {
        return data;
    }

protected:
    query::detail::SelectSpec data;
};

template <typename Source, typename Result>
class RuntimeProjectionQuery : public RuntimeQuery<Source>
{
public:
    template <typename... Projections>
    auto project(Projections... projections) -> RuntimeProjectionQuery&
    {
        this->data.projections = {std::move(projections)...};
        orm::detail::validateProjectionAliases<Result>(this->data.projections);
        return *this;
    }
};

template <typename Model>
class RuntimeUpdate
{
public:
    template <typename Value>
    auto set(query::detail::Column column, Value value) -> RuntimeUpdate&
    {
        data.assignments.push_back(query::detail::UpdateAssignment{
            .column = std::move(column),
            .value = query::detail::UpdateValue{.value = query::QueryValue{std::move(value)}}});
        return *this;
    }

    template <typename Value>
    auto set(query::detail::Column column, const std::optional<Value>& value) -> RuntimeUpdate&
    {
        if (value.has_value())
        {
            set(std::move(column), value.value());
        }
        else
        {
            set(std::move(column), std::nullopt);
        }
        return *this;
    }

    auto set(query::detail::Column column, std::nullopt_t) -> RuntimeUpdate&
    {
        data.assignments.push_back(
            query::detail::UpdateAssignment{.column = std::move(column), .value = query::detail::UpdateValue{}});
        return *this;
    }

    auto where(const query::detail::Predicate& predicate) -> RuntimeUpdate&
    {
        data.predicate = predicate;
        return *this;
    }

    auto andWhere(const query::detail::Predicate& predicate) -> RuntimeUpdate&
    {
        data.predicate = data.predicate.has_value() ? data.predicate.value() && predicate : predicate;
        return *this;
    }

    auto orWhere(const query::detail::Predicate& predicate) -> RuntimeUpdate&
    {
        data.predicate = data.predicate.has_value() ? data.predicate.value() || predicate : predicate;
        return *this;
    }

    [[nodiscard]] auto getData() const -> const query::detail::UpdateSpec&
    {
        return data;
    }

private:
    query::detail::UpdateSpec data;
};
} // namespace orm::tests
