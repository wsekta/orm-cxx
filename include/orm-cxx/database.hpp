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

    template <typename T>
    auto createTable() -> void
    {
        Model<T> model;

        std::string query = "CREATE TABLE IF NOT EXISTS " + model.getTableName() + " (";

        auto columnNames = model.getColumnNames();

        for (auto& columnName : columnNames)
        {
            query += columnName + " " + /*model::getBinding<T>(columnName)*/ "int" + ", ";
        }

        query.pop_back();
        query.pop_back();

        query += ");";

        sql << query;
    }

    template <typename T>
    auto deleteTable() -> void
    {
        Model<T> model;

        std::string query = "DROP TABLE IF EXISTS " + model.getTableName() + ";";

        sql << query;
    }

private:
    soci::session sql;
};
}