#pragma once

#include "model.hpp"
#include "query.hpp"
#include "model/Binding.hpp"

namespace orm
{
class Database
{
public:
    Database() : sql() {}

    void connect(const std::string& connectionString);

    template <typename T>
    auto executeQuery(Query<T>& query) -> std::vector<T>
    {
        std::vector<T> result;

        sql << query.buildQuery(), soci::into(result);

        return result;
    }

private:
    soci::session sql;
};
}