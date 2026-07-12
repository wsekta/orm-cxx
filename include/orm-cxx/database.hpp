#pragma once

#include <cassert>
#include <cstddef>
#include <algorithm>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeindex>
#include <typeinfo>
#include <utility>
#include <variant>
#include <vector>

#include "database/BackendType.hpp"
#include "database/binding/Binding.hpp"
#include "database/binding/ProjectionBinding.hpp"
#include "database/CommandGeneratorFactory.hpp"
#include "database/RelationStatements.hpp"
#include "database/Statement.hpp"
#include "database/binding/CollectionBinding.hpp"
#include "projection_query.hpp"
#include "query.hpp"
#include "soci/soci.h"
#include "soci/values.h"
#include "update.hpp"

namespace orm
{
namespace detail
{
template <typename T>
inline auto bindTypedNullStatementParameter(soci::values& values, const std::string& name, const T& value) -> void
{
    values.set(name, value);
    values.set(name, value, soci::i_null);
}

inline auto bindNullStatementParameter(soci::values& values, const db::StatementParameter& parameter) -> void
{
    switch (parameter.nullType)
    {
    case model::ColumnType::Bool:
    case model::ColumnType::Char:
    case model::ColumnType::UnsignedChar:
    case model::ColumnType::Short:
    case model::ColumnType::UnsignedShort:
    case model::ColumnType::Int:
        bindTypedNullStatementParameter(values, parameter.name, int{});
        return;
    case model::ColumnType::UnsignedInt:
    case model::ColumnType::UnsignedLongLong:
        bindTypedNullStatementParameter(values, parameter.name, static_cast<unsigned long long>(0));
        return;
    case model::ColumnType::LongLong:
        bindTypedNullStatementParameter(values, parameter.name, static_cast<long long>(0));
        return;
    case model::ColumnType::Float:
    case model::ColumnType::Double:
        bindTypedNullStatementParameter(values, parameter.name, 0.0);
        return;
    case model::ColumnType::String:
        bindTypedNullStatementParameter(values, parameter.name, std::string{});
        return;
    case model::ColumnType::Uuid:
    case model::ColumnType::Unknown:
    case model::ColumnType::OneToOne:
        throw std::invalid_argument{"Cannot bind NULL parameter with unsupported column type"};
    }

    throw std::invalid_argument{"Cannot bind NULL parameter with unsupported column type"};
}

inline auto bindStatementParameter(soci::values& values, const db::StatementParameter& parameter) -> void
{
    if (parameter.value.has_value())
    {
        std::visit([&values, &parameter](const auto& value) { values.set(parameter.name, value); },
                   parameter.value->get());
        return;
    }

    bindNullStatementParameter(values, parameter);
}

inline auto bindStatementParameters(soci::values& values, const std::vector<db::StatementParameter>& parameters) -> void
{
    for (const auto& parameter : parameters)
    {
        bindStatementParameter(values, parameter);
    }
}

auto normalizeAffectedRows(long long affectedRows) -> std::size_t;
} // namespace detail

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

    template <typename T>
    using ProjectionPayload = db::binding::ProjectionPayload<T>;

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

        auto statement = commandGeneratorFactory.getCommandGenerator(backendType).select(query.getData());

        soci::values parameterValues;

        for (const auto& parameter : statement.parameters)
        {
            detail::bindStatementParameter(parameterValues, parameter);
        }

        auto readRows = [&]<bool JoinedValues>()
        {
            soci::rowset<db::binding::BindingPayload<T, JoinedValues>> preparedRowSet =
                (sql.prepare << statement.sql, soci::use(parameterValues));

            for (auto& payload : preparedRowSet)
            {
                result.push_back(std::move(payload.value));
            }
        };

        if (query.getData().shouldJoin)
        {
            readRows.template operator()<true>();
        }
        else
        {
            readRows.template operator()<false>();
        }

        loadIncludedCollections(query.getData(), result);

        return result;
    }

    /**
     * @brief Executes a projection select query and returns DTO results.
     *
     * @tparam Source The source ORM model.
     * @tparam Result The flat projection DTO.
     * @param query The projection query to execute.
     * @return The vector of DTO objects returned by the projection query.
     */
    template <typename Source, typename Result>
    auto select(ProjectionQuery<Source, Result>& query) -> std::vector<Result>
    {
        std::vector<Result> result;

        auto statement = commandGeneratorFactory.getCommandGenerator(backendType).select(query.getData());

        soci::values parameterValues;

        for (const auto& parameter : statement.parameters)
        {
            detail::bindStatementParameter(parameterValues, parameter);
        }

        soci::rowset<ProjectionPayload<Result>> preparedRowSet =
            (sql.prepare << statement.sql, soci::use(parameterValues));

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
     * @brief Executes an update query.
     *
     * @tparam T The type of the query model.
     * @param update The update builder with assignments and a required predicate.
     * @return The number of affected rows.
     */
    template <typename T>
    auto update(const Update<T>& update) -> std::size_t
    {
        const auto statement = commandGeneratorFactory.getCommandGenerator(backendType).update(update.getData());

        return executeMutation(statement);
    }

    /**
     * @brief Executes a delete query for rows matching a predicate.
     *
     * @tparam T The type of the model whose rows will be deleted.
     * @param predicate The required predicate used in the WHERE clause.
     * @return The number of affected rows.
     */
    template <typename T>
    auto remove(const query::Predicate& predicate) -> std::size_t
    {
        static auto modelInfo = Model<T>::getModelInfo();
        const auto statement = commandGeneratorFactory.getCommandGenerator(backendType).remove(modelInfo, predicate);

        return executeMutation(statement);
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
     * @brief Creates junction tables owned by a model's ManyToMany mappings.
     *
     * Endpoint tables must already exist. Inverse mappings intentionally do
     * not create the shared junction table.
     */
    template <typename T>
    auto createRelationTables() -> void
    {
        const auto& modelInfo = Model<T>::getModelInfo();

        ensureRelationTableEndpointsExist(modelInfo);

        for (const auto& command : db::relations::createTableStatements(modelInfo))
        {
            sql << command;
        }
    }

    /**
     * @brief Drops junction tables owned by a model's ManyToMany mappings.
     */
    template <typename T>
    auto deleteRelationTables() -> void
    {
        const auto& modelInfo = Model<T>::getModelInfo();

        for (const auto& command : db::relations::dropTableStatements(modelInfo))
        {
            sql << command;
        }
    }

    /**
     * @brief Creates or changes a OneToMany/ManyToMany association.
     * @return 1 when database state changed, otherwise 0.
     */
    template <typename Owner, typename Target>
    auto link(const Owner& owner, std::string_view relationField, const Target& target) -> std::size_t
    {
        const auto& ownerInfo = Model<Owner>::getModelInfo();
        const auto relation =
            std::ranges::find_if(ownerInfo.relationsInfo, [&relationField](const auto& candidate)
                                 { return candidate.fieldName == relationField; });

        if (relation == ownerInfo.relationsInfo.end() or relation->kind == model::RelationKind::ToOne)
        {
            throw std::invalid_argument{"Unknown collection relation: " + std::string{relationField}};
        }

        if (relation->targetType != std::type_index{typeid(Target)})
        {
            throw std::invalid_argument{"Relation target type does not match mapping: " +
                                        std::string{relationField}};
        }

        const auto ownerKey = db::binding::getPrimaryKey(owner);
        const auto targetKey = db::binding::getPrimaryKey(target);

        if (not relationEndpointExists(ownerInfo, ownerKey) or
            not relationEndpointExists(relation->targetModel(), targetKey))
        {
            throw std::invalid_argument{"Cannot link relation endpoints that do not exist"};
        }

        return executeMutation(db::relations::linkStatement(ownerInfo, *relation, ownerKey, targetKey));
    }

    /**
     * @brief Removes a OneToMany/ManyToMany association.
     * @return 1 when database state changed, otherwise 0.
     */
    template <typename Owner, typename Target>
    auto unlink(const Owner& owner, std::string_view relationField, const Target& target) -> std::size_t
    {
        const auto& ownerInfo = Model<Owner>::getModelInfo();
        const auto relation =
            std::ranges::find_if(ownerInfo.relationsInfo, [&relationField](const auto& candidate)
                                 { return candidate.fieldName == relationField; });

        if (relation == ownerInfo.relationsInfo.end() or relation->kind == model::RelationKind::ToOne)
        {
            throw std::invalid_argument{"Unknown collection relation: " + std::string{relationField}};
        }

        if (relation->targetType != std::type_index{typeid(Target)})
        {
            throw std::invalid_argument{"Relation target type does not match mapping: " +
                                        std::string{relationField}};
        }

        return executeMutation(db::relations::unlinkStatement(ownerInfo, *relation,
                                                              db::binding::getPrimaryKey(owner),
                                                              db::binding::getPrimaryKey(target)));
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
    template <typename Owner, typename Target, bool JoinedValues>
    auto appendCollectionRows(const db::Statement& statement,
                              std::map<db::binding::PrimaryKey, std::vector<Target>>& groupedTargets) -> void
    {
        soci::values parameterValues;
        detail::bindStatementParameters(parameterValues, statement.parameters);
        soci::rowset<db::binding::CollectionPayload<Owner, Target, JoinedValues>> preparedRowSet =
            (sql.prepare << statement.sql, soci::use(parameterValues));

        for (auto& payload : preparedRowSet)
        {
            groupedTargets[payload.ownerKey].push_back(std::move(payload.value));
        }
    }

    template <typename Owner>
    auto loadIncludedCollections(const query::QueryData& queryData, std::vector<Owner>& owners) -> void
    {
        if (queryData.includes.empty())
        {
            return;
        }

        const auto& ownerInfo = queryData.modelInfo;

        for (const auto& includedRelation : queryData.includes)
        {
            const auto* relation = ownerInfo.findRelation(includedRelation);

            if (relation == nullptr or relation->kind == model::RelationKind::ToOne)
            {
                throw std::invalid_argument{"Unknown collection relation: " + includedRelation};
            }
        }

        if (owners.empty())
        {
            return;
        }

        const auto reflectedFields = rfl::fields<Owner>();
        auto ownerFields = rfl::to_view(owners.front()).values();
        auto loadField = [this, &queryData, &owners, &ownerInfo, &reflectedFields](auto fieldIndex, auto* field)
        {
            using collection_t = std::decay_t<decltype(*field)>;

            if constexpr (orm::is_relation_collection_v<collection_t>)
            {
                const auto relationName = std::string{reflectedFields[fieldIndex].name()};

                if (std::ranges::find(queryData.includes, relationName) == queryData.includes.end())
                {
                    return;
                }

                const auto* relation = ownerInfo.findRelation(relationName);
                assert(relation != nullptr);

                loadCollectionField<decltype(fieldIndex)::value, Owner, collection_t>(queryData, owners, *relation);
            }
        };

        utils::constexpr_for_tuple(ownerFields, loadField);
    }

    template <std::size_t FieldIndex, typename Owner, typename Collection>
    auto loadCollectionField(const query::QueryData& queryData, std::vector<Owner>& owners,
                             const model::RelationInfo& relation) -> void
    {
        using target_t = orm::relation_target_t<Collection>;

        if (relation.targetType != std::type_index{typeid(target_t)})
        {
            throw std::invalid_argument{"Collection wrapper target does not match relation metadata: " +
                                        relation.fieldName};
        }

        const auto ownerPrimaryKey = db::binding::getPrimaryKeyColumns(queryData.modelInfo);
        constexpr std::size_t parameterBudget = 900;
        const auto batchSize = std::max<std::size_t>(1, parameterBudget / ownerPrimaryKey.size());
        std::map<db::binding::PrimaryKey, std::vector<target_t>> groupedTargets;

        orm::Query<target_t> targetQuery;

        if (not queryData.shouldJoin)
        {
            targetQuery.disableJoining();
        }

        const auto baseTargetStatement =
            commandGeneratorFactory.getCommandGenerator(backendType).select(targetQuery.getData());

        assert(baseTargetStatement.parameters.empty());

        for (std::size_t batchStart = 0; batchStart < owners.size(); batchStart += batchSize)
        {
            const auto batchEnd = std::min(owners.size(), batchStart + batchSize);
            std::vector<db::binding::PrimaryKey> ownerKeys;
            ownerKeys.reserve(batchEnd - batchStart);

            for (auto ownerIndex = batchStart; ownerIndex < batchEnd; ++ownerIndex)
            {
                ownerKeys.push_back(db::binding::getPrimaryKey(owners[ownerIndex]));
            }

            const auto statement = db::relations::collectionSelectStatement(
                queryData.modelInfo, relation, baseTargetStatement.sql, ownerKeys, queryData.shouldJoin);
            if (queryData.shouldJoin)
            {
                appendCollectionRows<Owner, target_t, true>(statement, groupedTargets);
            }
            else
            {
                appendCollectionRows<Owner, target_t, false>(statement, groupedTargets);
            }
        }

        for (auto& owner : owners)
        {
            const auto ownerKey = db::binding::getPrimaryKey(owner);
            auto ownerFields = rfl::to_view(owner).values();
            auto* collection = std::get<FieldIndex>(ownerFields);
            const auto targets = groupedTargets.find(ownerKey);

            if (targets == groupedTargets.end())
            {
                collection->setLoaded({});
            }
            else
            {
                collection->setLoaded(targets->second);
            }
        }
    }

    auto executeMutation(const db::Statement& statement) -> std::size_t;
    auto relationEndpointExists(const model::ModelInfo& modelInfo, const db::binding::PrimaryKey& key) -> bool;
    auto tableExists(std::string_view tableName) -> bool;
    auto ensureRelationTableEndpointsExist(const model::ModelInfo& ownerInfo) -> void;

    soci::session sql;
    std::unique_ptr<soci::transaction> transaction;
    db::BackendType backendType;
    db::CommandGeneratorFactory commandGeneratorFactory;
};
} // namespace orm
