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
     * @param query The query of type T to execute.
     * @return The vector of objects of type T returned by the query.
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
     * @param objects The vector of objects of type T to insert.
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
     * @param object The object of type T to insert.
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
     * @tparam T The type of the model which table to will be created.
     */
    template <typename T>
    auto createTable() -> void
    {
        auto modelInfo = Model<T>::getModelInfo();

        std::string query = "CREATE TABLE IF NOT EXISTS " + modelInfo.tableName + " (\n";

        auto& columns = modelInfo.columnsInfo;

        for (auto& column : columns)
        {
            if (column.isForeignModel)
            {
                query += addColumnsForForeignIds(modelInfo.foreignIdsInfo, column);

                continue;
            }

            auto sqlType = typeTranslatorFactory.getTranslator(backendType)->toSqlType(column.type);

            if (column.isNotNull)
            {
                sqlType += " NOT NULL";
            }

            query += "\t" + column.name + " " + sqlType + ",\n";
        }

        if (modelInfo.idColumnsNames.empty())
        {
            query.pop_back();
            query.pop_back();
        }
        else
        {
            query += "\tPRIMARY KEY (";

            for (auto& idColumn : modelInfo.idColumnsNames)
            {
                query += idColumn + ", ";
            }

            query.pop_back();
            query.pop_back();

            query += ")";
        }

        if (not modelInfo.foreignIdsInfo.empty())
        {
            query += ",\n" + addForeignIds(modelInfo.foreignIdsInfo);
        }

        query += "\n);";

        std::cout << query << std::endl;

        sql << query;
    }

    /**
     * @brief Execute a delete table query for a model.
     *
     * @tparam T The type of the model which table to will be deleted.
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

    /**
     * @brief Add columns for foreign ids to a query.
     *
     * @param query The query to add the columns to.
     * @param foreignIdsInfo The foreign ids info for the model.
     * @param columnInfo The column info for the model.
     */
    auto addColumnsForForeignIds(const model::ForeignIdsInfo& foreignIdsInfo, const model::ColumnInfo& columnInfo)
        -> std::string;

    /**
     * @brief Add foreign ids to a model.
     *
     * @param foreignIdsInfo The foreign ids info for the model.
     * @return
     */
    auto addForeignIds(const model::ForeignIdsInfo& foreignIdsInfo) -> std::string;
};
}
