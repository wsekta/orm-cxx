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
    auto executeQuery(Query<T>& query) -> std::vector<T>
    {
        std::vector<T> result;

        soci::rowset<Model<T>> preparedRowSet = (sql.prepare << query.buildQuery());

        for (auto& model : preparedRowSet)
        {
            result.push_back(std::move(model.getObject()));
        }

        return result;
    }

private:
    soci::session sql;
};
}