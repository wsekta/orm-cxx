#pragma once

#include "orm-cxx/projection_query.hpp"
#include "orm-cxx/query.hpp"
#include "orm-cxx/update.hpp"

namespace orm
{
template <typename SchemaType>
class Database
{
public:
    template <class T>
    static auto getSelectSpec(Query<T>& query) -> const query::SelectSpec&
    {
        return query.getData();
    }

    template <class Source, class Result>
    static auto getSelectSpec(ProjectionQuery<Source, Result>& query) -> const query::SelectSpec&
    {
        return query.getData();
    }

    template <class T>
    static auto getUpdateSpec(Update<T>& update) -> const query::UpdateSpec&
    {
        return update.getData();
    }
};

using FakeDatabase = Database<void>;
} // namespace orm
