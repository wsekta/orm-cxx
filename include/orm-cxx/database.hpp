#pragma once

#include <iostream>

#include "database/BackendType.hpp"
#include "database/TypeTranslatorFactory.hpp"
#include "model.hpp"
#include "model/Binding.hpp"
#include "query.hpp"
#include "soci/soci.h"

namespace orm
{
class Database
{
public:
    Database();

    auto connect(const std::string& connectionString) -> void;

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
    auto insertObjects(const std::vector<T>& objects) -> void
    {
        for (const auto& object : objects)
        {
            insertObject(object);
        }
    }

    template <typename T>
    auto insertObject(T object) -> void
    {
        Model<T> model;

        // TODO: Implement
    }

    template <typename T>
    auto createTable() -> void
    {
        Model<T> model;

        std::string query = "CREATE TABLE IF NOT EXISTS " + model.getTableName() + " (";

        auto columns = model.getColumnsInfo();

        for (auto& column : columns)
        {
            auto sqlType = typeTranslatorFactory.getTranslator(backendType)->toSqlType(column.type);

            query += column.name + " " + sqlType + ", ";
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

    auto getBackendType() -> db::BackendType;

private:
    soci::session sql;
    db::BackendType backendType;
    db::TypeTranslatorFactory typeTranslatorFactory;
};
}