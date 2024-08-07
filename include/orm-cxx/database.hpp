#pragma once

#include "database/BackendType.hpp"
#include "database/binding/Binding.hpp"
#include "database/CommandGeneratorFactory.hpp"
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
    template <typename T>
    using Payload = db::binding::BindingPayload<T>;

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
     * @brief Disconnects from the database.
     */
    auto disconnect() -> void;

    /**
     * @brief Executes a select query and returns the result.
     *
     * @tparam T The type of the query model.
     * @param query The select query of type T to execute.
     * @return The vector of objects of type T returned by the select query.
     */
    template <typename T>
    auto select(Query<T>& query) -> std::vector<T>
    {
        std::vector<T> result;

        auto command = commandGeneratorFactory.getCommandGenerator(backendType).select(query.getData());

        Payload<T>::bindingInfo.joinedValues = query.getData().shouldJoin;

        soci::rowset<Payload<T>> preparedRowSet = (sql.prepare << command);

        for (auto& payload : preparedRowSet)
        {
            result.push_back(std::move(payload.value));
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
    auto insert(const std::vector<T>& objects) -> void
    {
        for (const auto& object : objects)
        {
            insert(object);
        }
    }

    /**
     * @brief Executes a insert query for a single object.
     *
     * @tparam T The type of the query.
     * @param object The object of type T to insert.
     */
    template <typename T>
    auto insert(T object) -> void
    {
        static auto command = commandGeneratorFactory.getCommandGenerator(backendType).insert(Model<T>::getModelInfo());

        const Payload<T> payload{};

        payload.value = std::move(object);

        sql << command, soci::use(payload);
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

        static auto command = commandGeneratorFactory.getCommandGenerator(backendType).createTable(modelInfo);

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

        static auto command = commandGeneratorFactory.getCommandGenerator(backendType).dropTable(model.getModelInfo());

        sql << command;
    }

    /**
     * @brief Get the backend type of the database.
     *
     * @return The backend type of the database.
     */
    auto getBackendType() -> db::BackendType;

    /**
     * @brief Starts a transaction.
     */
    auto beginTransaction() -> void;

    /**
     * @brief Commits a transaction.
     */
    auto commitTransaction() -> void;

    /**
     * @brief Rollbacks a transaction.
     */
    auto rollbackTransaction() -> void;

private:
    soci::session sql;
    std::unique_ptr<soci::transaction> transaction;
    db::BackendType backendType;
    db::CommandGeneratorFactory commandGeneratorFactory;
};
} // namespace orm
