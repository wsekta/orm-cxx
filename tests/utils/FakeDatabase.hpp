#pragma once

#include "orm-cxx/query.hpp"
#include "orm-cxx/update.hpp"

namespace orm
{
class Database
{
public:
    template <class T>
    static auto getQueryData(Query<T>& query) -> const query::QueryData&
    {
        return query.getData();
    }

    template <class T>
    static auto getUpdateData(Update<T>& update) -> const query::UpdateData&
    {
        return update.getData();
    }
};
} // namespace orm
