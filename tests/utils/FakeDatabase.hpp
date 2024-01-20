#pragma once

#include "orm-cxx/query.hpp"

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
};
}