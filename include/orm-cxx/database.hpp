#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <limits>
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

#include "database/BackendCapabilities.hpp"
#include "database/BackendRuntime.hpp"
#include "database/BackendType.hpp"
#include "database/binding/Binding.hpp"
#include "database/binding/CollectionBinding.hpp"
#include "database/binding/ConversionError.hpp"
#include "database/binding/ProjectionBinding.hpp"
#include "database/binding/StatementBinding.hpp"
#include "database/CommandGeneratorFactory.hpp"
#include "database/DatabaseError.hpp"
#include "database/RelationStatements.hpp"
#include "database/Statement.hpp"
#include "projection_query.hpp"
#include "query.hpp"
#include "soci/soci.h"
#include "soci/values.h"
#include "update.hpp"

namespace orm
{
namespace detail
{
inline auto bindStatementParameter(const db::BackendRuntime& runtime, soci::values& values,
                                   const db::StatementParameter& parameter) -> void
{
    runtime.bind(values, parameter.name, parameter.getBoundValue());
}

inline auto bindStatementParameters(const db::BackendRuntime& runtime, soci::values& values,
                                    const std::vector<db::StatementParameter>& parameters) -> void
{
    for (const auto& parameter : parameters)
    {
        bindStatementParameter(runtime, values, parameter);
    }
}

auto bindModelParameters(const db::BackendRuntime& runtime, soci::values& targetValues,
                         const soci::values& serializedModel, const model::ModelInfo& modelInfo) -> std::size_t;
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
     * @brief Constructs a database with an application-supplied backend registry.
     */
    explicit Database(db::CommandGeneratorFactory factory);
    Database(const Database&) = delete;
    Database(Database&&) = delete;
    auto operator=(const Database&) -> Database& = delete;
    auto operator=(Database&&) -> Database& = delete;

    /**
     * @brief Connects to a database.
     *
     * @param connectionString The connection string for the database.
     */
    auto connect(const std::string& connectionString) -> void;

    /**
     * @brief Connects using an explicitly selected registered backend.
     */
    auto connect(db::BackendType requestedBackend, const std::string& connectionString) -> void;

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
        ensureQuerySupported(query.getData());
        std::vector<T> result;
        const auto statement = getCommandGenerator().select(query.getData());
        ensureStatementWithinBindLimit(statement.parameters.size(), "select");

        try
        {
            soci::values parameterValues;
            detail::bindStatementParameters(getBackend().runtime(), parameterValues, statement.parameters);

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
        }
        catch (const db::binding::ConversionError&)
        {
            throw DatabaseError{DatabaseErrorCode::Conversion, backendType, "select",
                                "A database result cannot be represented by the requested model"};
        }
        catch (const soci::soci_error& error)
        {
            throwTranslatedError(error, DatabaseErrorCode::Statement, "select");
        }

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
        ensureQuerySupported(query.getData());
        std::vector<Result> result;
        const auto statement = getCommandGenerator().select(query.getData());
        ensureStatementWithinBindLimit(statement.parameters.size(), "select projection");

        try
        {
            soci::values parameterValues;
            detail::bindStatementParameters(getBackend().runtime(), parameterValues, statement.parameters);

            soci::rowset<ProjectionPayload<Result>> preparedRowSet =
                (sql.prepare << statement.sql, soci::use(parameterValues));

            for (auto& payload : preparedRowSet)
            {
                result.push_back(std::move(payload.value));
            }
        }
        catch (const db::binding::ConversionError&)
        {
            throw DatabaseError{DatabaseErrorCode::Conversion, backendType, "select projection",
                                "A database result cannot be represented by the requested projection"};
        }
        catch (const soci::soci_error& error)
        {
            throwTranslatedError(error, DatabaseErrorCode::Statement, "select projection");
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
        const auto& modelInfo = Model<T>::getModelInfo();
        ensureModelSupported(modelInfo, "insert");
        requireCapability(getBackendCapabilities().mutations.insert, "insert", "insert is not supported");
        const auto command = getCommandGenerator().insert(modelInfo);

        const Payload<T> payload{};

        payload.value = std::move(object);

        try
        {
            soci::values serializedModel;
            auto indicator = soci::i_ok;
            soci::type_conversion<Payload<T>>::to_base(payload, serializedModel, indicator);

            soci::values parameterValues;
            const auto parameterCount =
                detail::bindModelParameters(getBackend().runtime(), parameterValues, serializedModel, modelInfo);
            ensureStatementWithinBindLimit(parameterCount, "insert");

            if (parameterCount == 0)
            {
                sql << command;
            }
            else
            {
                sql << command, soci::use(parameterValues);
            }
        }
        catch (const db::binding::ConversionError&)
        {
            throw DatabaseError{DatabaseErrorCode::Conversion, backendType, "insert",
                                "A model value cannot be represented by the selected backend"};
        }
        catch (const soci::soci_error& error)
        {
            throwTranslatedError(error, DatabaseErrorCode::Statement, "insert");
        }
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
        ensureModelSupported(update.getData().modelInfo, "update");
        requireCapability(getBackendCapabilities().mutations.update, "update", "update is not supported");
        const auto statement = getCommandGenerator().update(update.getData());

        return executeMutation(statement, "update");
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
        ensureModelSupported(modelInfo, "remove");
        requireCapability(getBackendCapabilities().mutations.remove, "remove", "remove is not supported");
        const auto statement = getCommandGenerator().remove(modelInfo, predicate);

        return executeMutation(statement, "remove");
    }

    /**
     * @brief Execute a create table query for a model.
     *
     * @tparam T The type of the model which table to will be created.
     */
    template <typename T>
    auto createTable() -> void
    {
        const auto& modelInfo = Model<T>::getModelInfo();
        ensureModelSupported(modelInfo, "create table");
        requireCapability(getBackendCapabilities().schema.createTableIfNotExists, "create table",
                          "idempotent table creation is not supported");
        const auto command = getCommandGenerator().createTable(modelInfo);
        executeSql(command, "create table");
    }

    /**
     * @brief Execute a delete table query for a model.
     *
     * @tparam T The type of the model which table to will be deleted.
     */
    template <typename T>
    auto deleteTable() -> void
    {
        const auto& modelInfo = Model<T>::getModelInfo();
        ensureModelSupported(modelInfo, "drop table");
        requireCapability(getBackendCapabilities().schema.dropTableIfExists, "drop table",
                          "idempotent table removal is not supported");
        const auto command = getCommandGenerator().dropTable(modelInfo);
        executeSql(command, "drop table");
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
        const auto ownsJunctionTable =
            std::ranges::any_of(modelInfo.relationsInfo,
                                [](const auto& relation)
                                {
                                    return relation.kind == model::RelationKind::ManyToMany and
                                           relation.junction.has_value() and relation.junction->owningSide;
                                });

        if (not ownsJunctionTable)
        {
            return;
        }

        ensureModelSupported(modelInfo, "create relation tables");
        ensureRelationTableEndpointsExist(modelInfo);

        for (const auto& command : db::relations::createTableStatements(getBackend().dialect(), modelInfo))
        {
            executeSql(command, "create relation table");
        }
    }

    /**
     * @brief Drops junction tables owned by a model's ManyToMany mappings.
     */
    template <typename T>
    auto deleteRelationTables() -> void
    {
        const auto& modelInfo = Model<T>::getModelInfo();
        const auto ownsJunctionTable =
            std::ranges::any_of(modelInfo.relationsInfo,
                                [](const auto& relation)
                                {
                                    return relation.kind == model::RelationKind::ManyToMany and
                                           relation.junction.has_value() and relation.junction->owningSide;
                                });

        if (not ownsJunctionTable)
        {
            return;
        }

        ensureModelSupported(modelInfo, "drop relation tables");
        requireCapability(getBackendCapabilities().relations.junctionTables, "drop relation tables",
                          "junction tables are not supported");
        requireCapability(getBackendCapabilities().schema.dropTableIfExists, "drop relation tables",
                          "idempotent table removal is not supported");

        for (const auto& command : db::relations::dropTableStatements(getBackend().dialect(), modelInfo))
        {
            executeSql(command, "drop relation table");
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
        const auto relation = std::ranges::find_if(ownerInfo.relationsInfo, [&relationField](const auto& candidate)
                                                   { return candidate.fieldName == relationField; });

        if (relation == ownerInfo.relationsInfo.end() or relation->kind == model::RelationKind::ToOne)
        {
            throw std::invalid_argument{"Unknown collection relation: " + std::string{relationField}};
        }

        if (relation->targetType != std::type_index{typeid(Target)})
        {
            throw std::invalid_argument{"Relation target type does not match mapping: " + std::string{relationField}};
        }

        ensureModelSupported(ownerInfo, "link relation");
        ensureModelSupported(relation->targetModel(), "link relation");
        const auto ownerKey = db::binding::getPrimaryKey(owner);
        const auto targetKey = db::binding::getPrimaryKey(target);

        if (relation->kind == model::RelationKind::OneToMany)
        {
            requireCapability(getBackendCapabilities().relations.oneToMany, "link relation",
                              "one-to-many relations are not supported");
        }
        else
        {
            requireCapability(getBackendCapabilities().relations.manyToMany, "link relation",
                              "many-to-many relations are not supported");
            requireCapability(getBackendCapabilities().mutations.atomicInsertIfAbsent, "link relation",
                              "idempotent relation links are not supported");
        }
        requireCapability(relation->kind == model::RelationKind::OneToMany ? getBackendCapabilities().mutations.update :
                                                                             getBackendCapabilities().mutations.insert,
                          "link relation", "relation mutations are not supported");

        if (not relationEndpointExists(ownerInfo, ownerKey) or
            not relationEndpointExists(relation->targetModel(), targetKey))
        {
            throw std::invalid_argument{"Cannot link relation endpoints that do not exist"};
        }

        return executeMutation(
            db::relations::linkStatement(getBackend().dialect(), ownerInfo, *relation, ownerKey, targetKey),
            "link relation");
    }

    /**
     * @brief Removes a OneToMany/ManyToMany association.
     * @return 1 when database state changed, otherwise 0.
     */
    template <typename Owner, typename Target>
    auto unlink(const Owner& owner, std::string_view relationField, const Target& target) -> std::size_t
    {
        const auto& ownerInfo = Model<Owner>::getModelInfo();
        const auto relation = std::ranges::find_if(ownerInfo.relationsInfo, [&relationField](const auto& candidate)
                                                   { return candidate.fieldName == relationField; });

        if (relation == ownerInfo.relationsInfo.end() or relation->kind == model::RelationKind::ToOne)
        {
            throw std::invalid_argument{"Unknown collection relation: " + std::string{relationField}};
        }

        if (relation->targetType != std::type_index{typeid(Target)})
        {
            throw std::invalid_argument{"Relation target type does not match mapping: " + std::string{relationField}};
        }

        ensureModelSupported(ownerInfo, "unlink relation");
        ensureModelSupported(relation->targetModel(), "unlink relation");
        requireCapability(relation->kind == model::RelationKind::OneToMany ?
                              getBackendCapabilities().relations.oneToMany :
                              getBackendCapabilities().relations.manyToMany,
                          "unlink relation", "the requested collection relation is not supported");
        requireCapability(relation->kind == model::RelationKind::OneToMany ? getBackendCapabilities().mutations.update :
                                                                             getBackendCapabilities().mutations.remove,
                          "unlink relation", "relation mutations are not supported");

        return executeMutation(db::relations::unlinkStatement(getBackend().dialect(), ownerInfo, *relation,
                                                              db::binding::getPrimaryKey(owner),
                                                              db::binding::getPrimaryKey(target)),
                               "unlink relation");
    }

    /**
     * @brief Get the backend type of the database.
     *
     * @return The backend type of the database.
     */
    [[nodiscard]] auto getBackendType() const noexcept -> db::BackendType;

    /**
     * @brief Returns whether a backend session is currently open.
     */
    [[nodiscard]] auto isConnected() const noexcept -> bool;

    /**
     * @brief Returns the capabilities advertised by the connected backend.
     * @throws DatabaseError when no backend is connected.
     */
    [[nodiscard]] auto getBackendCapabilities() const -> const db::BackendCapabilities&;

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
        ensureStatementWithinBindLimit(statement.parameters.size(), "include collection");
        soci::values parameterValues;
        detail::bindStatementParameters(getBackend().runtime(), parameterValues, statement.parameters);
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
        const auto runtimeLimits = getBackendRuntimeLimits();
        const auto parameterBudget = runtimeLimits.maxBindParameters.value_or(std::numeric_limits<std::size_t>::max());

        if (parameterBudget < ownerPrimaryKey.size())
        {
            throw DatabaseError{DatabaseErrorCode::UnsupportedFeature, backendType, "include collection",
                                "The backend bind-parameter limit is too small for the relation primary key"};
        }

        const auto batchSize = runtimeLimits.maxBindParameters.has_value() ?
                                   std::max<std::size_t>(1, parameterBudget / ownerPrimaryKey.size()) :
                                   owners.size();
        std::map<db::binding::PrimaryKey, std::vector<target_t>> groupedTargets;

        orm::Query<target_t> targetQuery;

        if (not queryData.shouldJoin)
        {
            targetQuery.disableJoining();
        }

        const auto baseTargetStatement = getCommandGenerator().select(targetQuery.getData());

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

            const auto statement =
                db::relations::collectionSelectStatement(getBackend().dialect(), queryData.modelInfo, relation,
                                                         baseTargetStatement.sql, ownerKeys, queryData.shouldJoin);
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

    auto executeMutation(const db::Statement& statement, std::string_view operation) -> std::size_t;
    auto executeSql(std::string_view statement, std::string_view operation) -> void;
    auto relationEndpointExists(const model::ModelInfo& modelInfo, const db::binding::PrimaryKey& key) -> bool;
    auto tableExists(std::string_view tableName) -> bool;
    auto ensureRelationTableEndpointsExist(const model::ModelInfo& ownerInfo) -> void;
    [[nodiscard]] auto getBackend() const -> const db::BackendProvider&;
    [[nodiscard]] auto getCommandGenerator() const -> const db::CommandGenerator&;
    [[nodiscard]] auto getBackendRuntimeLimits() -> db::BackendRuntimeLimits;
    auto ensureStatementWithinBindLimit(std::size_t parameterCount, std::string_view operation) -> void;
    auto ensureModelSupported(const model::ModelInfo& modelInfo, std::string_view operation) const -> void;
    auto ensureQuerySupported(const query::QueryData& queryData) const -> void;
    auto ensureAffectedRowsAvailable(std::string_view operation) const -> void;
    auto requireCapability(bool supported, std::string_view operation, std::string_view message) const -> void;
    [[noreturn]] auto throwTranslatedError(const soci::soci_error& error, DatabaseErrorCode fallback,
                                           std::string_view operation) -> void;

    soci::session sql;
    std::unique_ptr<soci::transaction> transaction;
    bool transactionFailed = false;
    db::BackendType backendType;
    db::CommandGeneratorFactory commandGeneratorFactory;
    const db::BackendProvider* backend = nullptr;
};
} // namespace orm
