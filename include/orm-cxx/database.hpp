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
/**
 * @brief A class representing a database in the ORM framework.
 *
 * This class provides functionality for connecting to a database and executing queries.
 */
class Database
{
public:
    /**
     * @brief Constructs a new Database object.
     */
    Database();

    /**
     * @brief Connects to a database.
     *
     * @param connectionString The connection string for the database.
     */
    auto connect(const std::string& connectionString) -> void;

    /**
     * @brief Executes a query and returns the result.
     *
     * @tparam T The type of the query.
     * @param query The query to execute.
     * @return The result of the query.
     */
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

    /**
     * @brief Executes a insert query for multiple objects.
     *
     * @tparam T The type of the query.
     * @param query The query to execute.
     */
    template <typename T>
    auto insertObjects(const std::vector<T>& objects) -> void
    {
        for (const auto& object : objects)
        {
            insertObject(object);
        }
    }

    /**
     * @brief Executes a insert query for a single object.
     *
     * @tparam T The type of the query.
     * @param query The query to execute.
     */
    template <typename T>
    auto insertObject(T object) -> void
    {
        Model<T> model;

        std::string query = "INSERT INTO " + model.getModelInfo().tableName + " (";

        auto columns = model.getModelInfo().columnsInfo;

        for (auto& column : columns)
        {
            query += column.name + ", ";
        }

        query.pop_back();
        query.pop_back();

        query += ") VALUES (";

        for (auto& column : columns)
        {
            query += ":" + column.name + ", ";
        }

        query.pop_back();
        query.pop_back();

        query += ");";

        model.getObject() = std::move(object);
        sql << query, soci::use(model);
    }

    /**
     * @brief Execute a create table query for a model.
     *
     * @tparam T The type of the model.
     */
    template <typename T>
    auto createTable() -> void
    {
        Model<T> model;

        std::string query = "CREATE TABLE IF NOT EXISTS " + model.getModelInfo().tableName + " (";

        auto columns = model.getModelInfo().columnsInfo;

        for (auto& column : columns)
        {
            auto sqlType = typeTranslatorFactory.getTranslator(backendType)->toSqlType(column.type);

            if (column.isNotNull)
            {
                sqlType += " NOT NULL";
            }

            query += column.name + " " + sqlType + ", ";
        }

        query.pop_back();
        query.pop_back();

        query += ");";

        sql << query;
    }

    /**
     * @brief Execute a delete table query for a model.
     *
     * @tparam T The type of the model.
     */
    template <typename T>
    auto deleteTable() -> void
    {
        Model<T> model;

        std::string query = "DROP TABLE IF EXISTS " + model.getModelInfo().tableName + ";";

        sql << query;
    }

    /**
     * @brief Get the backend type of the database.
     *
     * @return The backend type of the database.
     */
    auto getBackendType() -> db::BackendType;

private:
    soci::session sql;
    db::BackendType backendType;
    db::TypeTranslatorFactory typeTranslatorFactory;
};
}
