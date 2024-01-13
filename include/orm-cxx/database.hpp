#pragma once

#include <iostream>

#include "database/BackendType.hpp"
#include "database/CommandGeneratorFactory.hpp"
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
        static Model<T> model;

        static auto command = commandGeneratorFactory.getCommandGenerator(backendType)->insert(model.getModelInfo());

        model.getObject() = std::move(object);

        sql << command, soci::use(model);
    }

    /**
     * @brief Execute a create table query for a model.
     *
     * @tparam T The type of the model which table to will be created.
     */
    template <typename T>
    auto createTable() -> void
    {
        static auto modelInfo = Model<T>::getModelInfo();

        static auto command = commandGeneratorFactory.getCommandGenerator(backendType)->createTable(modelInfo);

        sql << command;
    }

    /**
     * @brief Execute a delete table query for a model.
     *
     * @tparam T The type of the model which table to will be deleted.
     */
    template <typename T>
    auto deleteTable() -> void
    {
        static Model<T> model;

        static auto command = commandGeneratorFactory.getCommandGenerator(backendType)->dropTable(model.getModelInfo());

        sql << command;
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
    db::CommandGeneratorFactory commandGeneratorFactory;
};
}
