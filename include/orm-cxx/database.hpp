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
#include <type_traits>
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
#include "model/Schema.hpp"
#include "projection_query.hpp"
#include "query.hpp"
#include "reflection/Reflection.hpp"
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
                         const soci::values& serializedModel, model::ModelView model) -> std::size_t;
auto normalizeAffectedRows(long long affectedRows) -> std::size_t;
} // namespace detail

/**
 * @brief A class representing a database in the ORM framework.
 *
 * This class provides functionality for connecting to a database and executing queries.
 */
class DatabaseCore
{
public:
    /**
     * @brief Constructs a new Database object.
     */
    DatabaseCore();

    /**
     * @brief Constructs a database with an application-supplied backend registry.
     */
    explicit DatabaseCore(db::CommandGeneratorFactory factory);
    DatabaseCore(const DatabaseCore&) = delete;
    DatabaseCore(DatabaseCore&&) = delete;
    auto operator=(const DatabaseCore&) -> DatabaseCore& = delete;
    auto operator=(DatabaseCore&&) -> DatabaseCore& = delete;

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
protected:
    template <typename SchemaType, typename T>
    auto selectImpl(Query<T>& query) -> std::vector<T>
    {
        constexpr auto model = model::modelView<SchemaType, T>();
        ensureQuerySupported(model, query.getData());
        std::vector<T> result;
        const auto statement = getCommandGenerator().select(model, query.getData());
        ensureStatementWithinBindLimit(statement.parameters.size(), "select");

        try
        {
            soci::values parameterValues;
            detail::bindStatementParameters(getBackend().runtime(), parameterValues, statement.parameters);

            auto readRows = [&]<bool JoinedValues>()
            {
                soci::rowset<db::binding::BindingPayload<T, SchemaType, JoinedValues>> preparedRowSet =
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

            loadIncludedCollections<SchemaType>(model, query.getData(), result);
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
    template <typename SchemaType, typename Source, typename Result>
    auto selectImpl(ProjectionQuery<Source, Result>& query) -> std::vector<Result>
    {
        constexpr auto model = model::modelView<SchemaType, Source>();
        ensureQuerySupported(model, query.getData());
        std::vector<Result> result;
        const auto statement = getCommandGenerator().select(model, query.getData());
        ensureStatementWithinBindLimit(statement.parameters.size(), "select projection");

        try
        {
            soci::values parameterValues;
            detail::bindStatementParameters(getBackend().runtime(), parameterValues, statement.parameters);

            soci::rowset<db::binding::ProjectionPayload<Result>> preparedRowSet =
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
    template <typename SchemaType, typename T>
    auto insertImpl(const std::vector<T>& objects) -> void
    {
        for (const auto& object : objects)
        {
            insertImpl<SchemaType>(object);
        }
    }

    /**
     * @brief Executes a insert query for a single object.
     *
     * @tparam T The type of the query.
     * @param object The object of type T to insert.
     */
    template <typename SchemaType, typename T>
    auto insertImpl(T object) -> void
    {
        constexpr auto model = model::modelView<SchemaType, T>();
        ensureModelSupported(model, "insert");
        requireCapability(getBackendCapabilities().mutations.insert, "insert", "insert is not supported");
        const auto command = getCommandGenerator().insert(model);

        const db::binding::BindingPayload<T, SchemaType> payload{};

        payload.value = std::move(object);

        try
        {
            soci::values serializedModel;
            auto indicator = soci::i_ok;
            soci::type_conversion<db::binding::BindingPayload<T, SchemaType>>::to_base(payload, serializedModel,
                                                                                       indicator);

            soci::values parameterValues;
            const auto parameterCount =
                detail::bindModelParameters(getBackend().runtime(), parameterValues, serializedModel, model);
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
    template <typename SchemaType, typename T>
    auto updateImpl(const Update<T>& update) -> std::size_t
    {
        constexpr auto model = model::modelView<SchemaType, T>();
        ensureModelSupported(model, "update");
        requireCapability(getBackendCapabilities().mutations.update, "update", "update is not supported");
        const auto statement = getCommandGenerator().update(model, update.getData());

        return executeMutation(statement, "update");
    }

    /**
     * @brief Executes a delete query for rows matching a predicate.
     *
     * @tparam T The type of the model whose rows will be deleted.
     * @param predicate The required predicate used in the WHERE clause.
     * @return The number of affected rows.
     */
    template <typename SchemaType, typename T>
    auto removeImpl(const query::Predicate& predicate) -> std::size_t
    {
        constexpr auto model = model::modelView<SchemaType, T>();
        ensureModelSupported(model, "remove");
        requireCapability(getBackendCapabilities().mutations.remove, "remove", "remove is not supported");
        const auto statement = getCommandGenerator().remove(model, predicate);

        return executeMutation(statement, "remove");
    }

    /**
     * @brief Execute a create table query for a model.
     *
     * @tparam T The type of the model which table to will be created.
     */
    template <typename SchemaType, typename T>
    auto createTableImpl() -> void
    {
        constexpr auto model = model::modelView<SchemaType, T>();
        ensureModelSupported(model, "create table");
        requireCapability(getBackendCapabilities().schema.createTableIfNotExists, "create table",
                          "idempotent table creation is not supported");
        const auto command = getCommandGenerator().createTable(model);
        executeSql(command, "create table");
    }

    /**
     * @brief Execute a delete table query for a model.
     *
     * @tparam T The type of the model which table to will be deleted.
     */
    template <typename SchemaType, typename T>
    auto deleteTableImpl() -> void
    {
        constexpr auto model = model::modelView<SchemaType, T>();
        ensureModelSupported(model, "drop table");
        requireCapability(getBackendCapabilities().schema.dropTableIfExists, "drop table",
                          "idempotent table removal is not supported");
        const auto command = getCommandGenerator().dropTable(model);
        executeSql(command, "drop table");
    }

    /**
     * @brief Creates junction tables owned by a model's ManyToMany mappings.
     *
     * Endpoint tables must already exist. Inverse mappings intentionally do
     * not create the shared junction table.
     */
    template <typename SchemaType, typename T>
    auto createRelationTablesImpl() -> void
    {
        constexpr auto owner = model::modelView<SchemaType, T>();
        const auto ownsJunctionTable =
            std::ranges::any_of(owner->relations,
                                [](const auto& relation)
                                {
                                    return relation.kind == model::RelationKind::ManyToMany and
                                           relation.junction.isConfigured() and relation.junction.owningSide;
                                });

        if (not ownsJunctionTable)
        {
            return;
        }

        ensureModelSupported(owner, "create relation tables");
        ensureRelationTableEndpointsExist(owner);

        for (const auto& command : db::relations::createTableStatements(getBackend().dialect(), owner))
        {
            executeSql(command, "create relation table");
        }
    }

    /**
     * @brief Drops junction tables owned by a model's ManyToMany mappings.
     */
    template <typename SchemaType, typename T>
    auto deleteRelationTablesImpl() -> void
    {
        constexpr auto owner = model::modelView<SchemaType, T>();
        const auto ownsJunctionTable =
            std::ranges::any_of(owner->relations,
                                [](const auto& relation)
                                {
                                    return relation.kind == model::RelationKind::ManyToMany and
                                           relation.junction.isConfigured() and relation.junction.owningSide;
                                });

        if (not ownsJunctionTable)
        {
            return;
        }

        ensureModelSupported(owner, "drop relation tables");
        requireCapability(getBackendCapabilities().relations.junctionTables, "drop relation tables",
                          "junction tables are not supported");
        requireCapability(getBackendCapabilities().schema.dropTableIfExists, "drop relation tables",
                          "idempotent table removal is not supported");

        for (const auto& command : db::relations::dropTableStatements(getBackend().dialect(), owner))
        {
            executeSql(command, "drop relation table");
        }
    }

    /**
     * @brief Creates or changes a OneToMany/ManyToMany association.
     * @return 1 when database state changed, otherwise 0.
     */
    template <typename SchemaType, typename Owner, typename Target>
    auto linkImpl(const Owner& owner, std::string_view relationField, const Target& target) -> std::size_t
    {
        constexpr auto ownerDescriptor = model::modelView<SchemaType, Owner>();
        const auto* relation = ownerDescriptor.findRelation(relationField);

        if (relation == nullptr or relation->kind == model::RelationKind::ToOne)
        {
            throw std::invalid_argument{"Unknown collection relation: " + std::string{relationField}};
        }

        const auto targetDescriptor = ownerDescriptor.resolveTarget(*relation);
        if (targetDescriptor == nullptr or targetDescriptor->type != model::typeId<Target>())
        {
            throw std::invalid_argument{"Relation target type does not match mapping: " + std::string{relationField}};
        }

        ensureModelSupported(ownerDescriptor, "link relation");
        ensureModelSupported(*targetDescriptor, "link relation");
        const auto ownerKey = db::binding::getPrimaryKey<SchemaType>(owner);
        const auto targetKey = db::binding::getPrimaryKey<SchemaType>(target);

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

        if (not relationEndpointExists(ownerDescriptor, ownerKey) or
            not relationEndpointExists(*targetDescriptor, targetKey))
        {
            throw std::invalid_argument{"Cannot link relation endpoints that do not exist"};
        }

        return executeMutation(
            db::relations::linkStatement(getBackend().dialect(), ownerDescriptor, *relation, ownerKey, targetKey),
            "link relation");
    }

    /**
     * @brief Removes a OneToMany/ManyToMany association.
     * @return 1 when database state changed, otherwise 0.
     */
    template <typename SchemaType, typename Owner, typename Target>
    auto unlinkImpl(const Owner& owner, std::string_view relationField, const Target& target) -> std::size_t
    {
        constexpr auto ownerDescriptor = model::modelView<SchemaType, Owner>();
        const auto* relation = ownerDescriptor.findRelation(relationField);

        if (relation == nullptr or relation->kind == model::RelationKind::ToOne)
        {
            throw std::invalid_argument{"Unknown collection relation: " + std::string{relationField}};
        }

        const auto targetDescriptor = ownerDescriptor.resolveTarget(*relation);
        if (targetDescriptor == nullptr or targetDescriptor->type != model::typeId<Target>())
        {
            throw std::invalid_argument{"Relation target type does not match mapping: " + std::string{relationField}};
        }

        ensureModelSupported(ownerDescriptor, "unlink relation");
        ensureModelSupported(*targetDescriptor, "unlink relation");
        requireCapability(relation->kind == model::RelationKind::OneToMany ?
                              getBackendCapabilities().relations.oneToMany :
                              getBackendCapabilities().relations.manyToMany,
                          "unlink relation", "the requested collection relation is not supported");
        requireCapability(relation->kind == model::RelationKind::OneToMany ? getBackendCapabilities().mutations.update :
                                                                             getBackendCapabilities().mutations.remove,
                          "unlink relation", "relation mutations are not supported");

        return executeMutation(db::relations::unlinkStatement(getBackend().dialect(), ownerDescriptor, *relation,
                                                              db::binding::getPrimaryKey<SchemaType>(owner),
                                                              db::binding::getPrimaryKey<SchemaType>(target)),
                               "unlink relation");
    }

public:
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
    template <typename SchemaType, typename Owner, typename Target, bool JoinedValues>
    auto appendCollectionRows(const db::Statement& statement,
                              std::map<db::binding::PrimaryKey, std::vector<Target>>& groupedTargets) -> void
    {
        ensureStatementWithinBindLimit(statement.parameters.size(), "include collection");
        soci::values parameterValues;
        detail::bindStatementParameters(getBackend().runtime(), parameterValues, statement.parameters);
        soci::rowset<db::binding::CollectionPayload<Owner, Target, SchemaType, JoinedValues>> preparedRowSet =
            (sql.prepare << statement.sql, soci::use(parameterValues));

        for (auto& payload : preparedRowSet)
        {
            groupedTargets[payload.ownerKey].push_back(std::move(payload.value));
        }
    }

    template <typename SchemaType, typename Owner>
    auto loadIncludedCollections(model::ModelView ownerDescriptor, const query::SelectSpec& queryData,
                                 std::vector<Owner>& owners) -> void
    {
        if (queryData.includes.empty())
        {
            return;
        }

        for (const auto& includedRelation : queryData.includes)
        {
            const auto* relation = ownerDescriptor.findRelation(includedRelation);

            if (relation == nullptr or relation->kind == model::RelationKind::ToOne)
            {
                throw std::invalid_argument{"Unknown collection relation: " + includedRelation};
            }
        }

        if (owners.empty())
        {
            return;
        }

        constexpr auto reflectedFields = reflection::fields<Owner>();
        auto ownerFields = reflection::fieldPointers(owners.front());
        auto loadField = [this, &queryData, &owners, ownerDescriptor, &reflectedFields](auto fieldIndex, auto* field)
        {
            using collection_t = std::decay_t<decltype(*field)>;

            if constexpr (orm::is_relation_collection_v<collection_t>)
            {
                const auto relationName = std::string{reflectedFields[fieldIndex].name};

                if (std::ranges::find(queryData.includes, relationName) == queryData.includes.end())
                {
                    return;
                }

                const auto* relation = ownerDescriptor.findRelation(relationName);
                assert(relation != nullptr);

                loadCollectionField<SchemaType, decltype(fieldIndex)::value, Owner, collection_t>(
                    ownerDescriptor, queryData, owners, *relation);
            }
        };

        utils::constexpr_for_tuple(ownerFields, loadField);
    }

    template <typename SchemaType, std::size_t FieldIndex, typename Owner, typename Collection>
    auto loadCollectionField(model::ModelView ownerDescriptor, const query::SelectSpec& queryData,
                             std::vector<Owner>& owners, const model::RelationView& relation) -> void
    {
        using target_t = orm::relation_target_t<Collection>;

        const auto targetDescriptor = ownerDescriptor.resolveTarget(relation);
        if (targetDescriptor == nullptr or targetDescriptor->type != model::typeId<target_t>())
        {
            throw std::invalid_argument{"Collection wrapper target does not match relation metadata: " +
                                        std::string{relation.fieldName}};
        }

        const auto ownerPrimaryKey = db::binding::getPrimaryKeyColumns(ownerDescriptor);
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

        const auto baseTargetStatement = getCommandGenerator().select(*targetDescriptor, targetQuery.getData());

        assert(baseTargetStatement.parameters.empty());

        for (std::size_t batchStart = 0; batchStart < owners.size(); batchStart += batchSize)
        {
            const auto batchEnd = std::min(owners.size(), batchStart + batchSize);
            std::vector<db::binding::PrimaryKey> ownerKeys;
            ownerKeys.reserve(batchEnd - batchStart);

            for (auto ownerIndex = batchStart; ownerIndex < batchEnd; ++ownerIndex)
            {
                ownerKeys.push_back(db::binding::getPrimaryKey<SchemaType>(owners[ownerIndex]));
            }

            const auto statement =
                db::relations::collectionSelectStatement(getBackend().dialect(), ownerDescriptor, relation,
                                                         baseTargetStatement.sql, ownerKeys, queryData.shouldJoin);
            if (queryData.shouldJoin)
            {
                appendCollectionRows<SchemaType, Owner, target_t, true>(statement, groupedTargets);
            }
            else
            {
                appendCollectionRows<SchemaType, Owner, target_t, false>(statement, groupedTargets);
            }
        }

        for (auto& owner : owners)
        {
            const auto ownerKey = db::binding::getPrimaryKey<SchemaType>(owner);
            auto ownerFields = reflection::fieldPointers(owner);
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
    auto relationEndpointExists(model::ModelView model, const db::binding::PrimaryKey& key) -> bool;
    auto tableExists(std::string_view tableName) -> bool;
    auto ensureRelationTableEndpointsExist(model::ModelView owner) -> void;
    [[nodiscard]] auto getBackend() const -> const db::BackendProvider&;
    [[nodiscard]] auto getCommandGenerator() const -> const db::CommandGenerator&;
    [[nodiscard]] auto getBackendRuntimeLimits() -> db::BackendRuntimeLimits;
    auto ensureStatementWithinBindLimit(std::size_t parameterCount, std::string_view operation) -> void;
    auto ensureModelSupported(model::ModelView model, std::string_view operation) const -> void;
    auto ensureQuerySupported(model::ModelView model, const query::SelectSpec& spec) const -> void;
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

/**
 * @brief Database facade bound to one closed compile-time model schema.
 *
 * Backend selection and connection
 * state remain runtime concerns. Every model
 * operation is checked against SchemaType before the implementation is
 *
 * instantiated.
 */
template <typename SchemaType>
class Database final : public DatabaseCore
{
    static_assert(requires { SchemaType::view; }, "Database requires an orm::Schema<...> type");

    template <typename T>
    static consteval auto requireSchemaModel() -> void
    {
        model::requireSchemaModel<SchemaType, T>();
    }

public:
    using DatabaseCore::DatabaseCore;

    template <typename T>
    using Payload = db::binding::BindingPayload<T, SchemaType>;

    template <typename T>
    using ProjectionPayload = db::binding::ProjectionPayload<T>;

    template <typename T>
    auto select(Query<T>& query) -> std::vector<T>
    {
        requireSchemaModel<T>();
        return this->template selectImpl<SchemaType>(query);
    }

    template <typename Source, typename Result>
    auto select(ProjectionQuery<Source, Result>& query) -> std::vector<Result>
    {
        requireSchemaModel<Source>();
        return this->template selectImpl<SchemaType>(query);
    }

    template <typename T>
    auto insert(const std::vector<T>& objects) -> void
    {
        requireSchemaModel<T>();
        this->template insertImpl<SchemaType>(objects);
    }

    template <typename T>
    auto insert(T object) -> void
    {
        requireSchemaModel<T>();
        this->template insertImpl<SchemaType>(std::move(object));
    }

    template <typename T>
    auto update(const Update<T>& update) -> std::size_t
    {
        requireSchemaModel<T>();
        return this->template updateImpl<SchemaType>(update);
    }

    template <typename T>
    auto remove(const query::Predicate& predicate) -> std::size_t
    {
        requireSchemaModel<T>();
        return this->template removeImpl<SchemaType, T>(predicate);
    }

    template <typename T>
    auto createTable() -> void
    {
        requireSchemaModel<T>();
        this->template createTableImpl<SchemaType, T>();
    }

    template <typename T>
    auto deleteTable() -> void
    {
        requireSchemaModel<T>();
        this->template deleteTableImpl<SchemaType, T>();
    }

    template <typename T>
    auto createRelationTables() -> void
    {
        requireSchemaModel<T>();
        this->template createRelationTablesImpl<SchemaType, T>();
    }

    template <typename T>
    auto deleteRelationTables() -> void
    {
        requireSchemaModel<T>();
        this->template deleteRelationTablesImpl<SchemaType, T>();
    }

    template <typename Owner, typename Target>
    auto link(const Owner& owner, std::string_view relationField, const Target& target) -> std::size_t
    {
        requireSchemaModel<Owner>();
        requireSchemaModel<Target>();
        return this->template linkImpl<SchemaType>(owner, relationField, target);
    }

    template <typename Owner, typename Target>
    auto unlink(const Owner& owner, std::string_view relationField, const Target& target) -> std::size_t
    {
        requireSchemaModel<Owner>();
        requireSchemaModel<Target>();
        return this->template unlinkImpl<SchemaType>(owner, relationField, target);
    }
};
} // namespace orm
