#pragma once

#include "tests/utils/RuntimeQueryBuilder.hpp"

namespace orm
{
template <>
class Database<void>
{
public:
    template <class T>
    static auto getSelectSpec(const tests::RuntimeQuery<T>& query) -> const query::detail::SelectSpec&
    {
        return query.getData();
    }

    template <class Source, class Result>
    static auto
    getSelectSpec(const tests::RuntimeProjectionQuery<Source, Result>& query) -> const query::detail::SelectSpec&
    {
        return query.getData();
    }

    template <class T>
    static auto getUpdateSpec(const tests::RuntimeUpdate<T>& update) -> const query::detail::UpdateSpec&
    {
        return update.getData();
    }

    template <class T>
    static auto getSelectSpec(Query<T>& query) -> const query::detail::SelectSpec&
    {
        return query.getData();
    }

    template <class Source, class Result>
    static auto getSelectSpec(ProjectionQuery<Source, Result>& query) -> const query::detail::SelectSpec&
    {
        return query.getData();
    }

    template <class T>
    static auto getUpdateSpec(Update<T>& update) -> const query::detail::UpdateSpec&
    {
        return update.getData();
    }
};

using FakeDatabase = Database<void>;
} // namespace orm
