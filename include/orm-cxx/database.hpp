#pragma once

#include "model.hpp"
#include "model/Binding.hpp"
#include "query.hpp"
#include "soci/soci.h"

namespace orm
{
class Database
{
public:
    Database() : sql() {}

    void connect(const std::string& connectionString);

    template <typename T>
    auto executeQuery(Query<T>& query) -> Model<T>
    {
        Model<T> result;

        sql << query.buildQuery(), soci::into(result);

        return result;
    }

private:
    soci::session sql;
};
}