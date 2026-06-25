#pragma once

#include <optional>
#include <utility>

#include "model.hpp"
#include "query/UpdateData.hpp"

namespace orm
{
class Database;

template <typename T>
class Update
{
public:
    Update() : data{.modelInfo = Model<T>::getModelInfo()} {}

    template <typename Value>
    auto set(query::Column column, Value value) -> Update<T>&
    {
        data.assignments.push_back(query::UpdateAssignment{
            .column = std::move(column),
            .value = query::UpdateValue{.value = query::QueryValue{std::move(value)}}});

        return *this;
    }

    template <typename Value>
    auto set(query::Column column, const std::optional<Value>& value) -> Update<T>&
    {
        if (value.has_value())
        {
            data.assignments.push_back(query::UpdateAssignment{
                .column = std::move(column),
                .value = query::UpdateValue{.value = query::QueryValue{value.value()}}});
        }
        else
        {
            set(std::move(column), std::nullopt);
        }

        return *this;
    }

    auto set(query::Column column, std::nullopt_t) -> Update<T>&
    {
        data.assignments.push_back(query::UpdateAssignment{.column = std::move(column), .value = query::UpdateValue{}});

        return *this;
    }

    auto where(const query::Predicate& predicate) -> Update<T>&
    {
        data.predicate = predicate;

        return *this;
    }

    auto andWhere(const query::Predicate& predicate) -> Update<T>&
    {
        data.predicate = data.predicate.has_value() ? data.predicate.value() && predicate : predicate;

        return *this;
    }

    auto orWhere(const query::Predicate& predicate) -> Update<T>&
    {
        data.predicate = data.predicate.has_value() ? data.predicate.value() || predicate : predicate;

        return *this;
    }

private:
    friend class orm::Database;

    [[nodiscard]] auto getData() const -> const query::UpdateData&
    {
        return data;
    }

    query::UpdateData data;
};
} // namespace orm
