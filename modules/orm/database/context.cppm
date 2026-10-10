module;

#include <algorithm>
#include <cassert>
#include <compare>
#include <cstddef>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "soci/type-conversion-traits.h"

export module orm:database_context;

import orm.reflection;
import :foundation;
import :model;
import :expressions;
import :dynamic_query;
import :static_plan;
import :sql;
import :config;
import :backend_relations;
export import :database_result_binding;
export import :database_connection;

// Keep the three SOCI specializations with the public operations that instantiate them.
namespace soci
{
template <typename T, typename SchemaType, bool JoinedValues = false>
using BindingPayload = orm::db::binding::BindingPayload<T, SchemaType, JoinedValues>;

export
{
    template <typename T, typename SchemaType, bool JoinedValues>
    struct type_conversion<BindingPayload<T, SchemaType, JoinedValues>>
    {
        using base_type = values;

        [[maybe_unused]] static void from_base(const soci::values& values, indicator /*ind*/,
                                               BindingPayload<T, SchemaType, JoinedValues>& model)
        {
            auto& modelValue = model.value;
            auto modelAsTuple = orm::reflection::fieldPointers(modelValue);

            auto getObjectFromValues = [&model, &values](auto fieldIndex, auto* field)
            {
                using field_t = std::decay_t<decltype(*field)>;

                if constexpr (orm::is_relation_collection_v<field_t>)
                {
                    return;
                }
                else
                {
                    orm::db::binding::ObjectFieldFromValues<field_t>::get(field, model, fieldIndex, values);
                }
            };

            orm::utils::constexpr_for_tuple(modelAsTuple, getObjectFromValues);
        }

        [[maybe_unused]] static void to_base(const BindingPayload<T, SchemaType, JoinedValues>& model,
                                             soci::values& values, indicator& ind)
        {
            auto& modelValue = model.value;
            auto modelAsTuple = orm::reflection::fieldPointers(modelValue);

            auto setObjectToValues = [&model, &values](auto fieldIndex, const auto* field)
            {
                using field_t = std::decay_t<decltype(*field)>;

                if constexpr (orm::is_relation_collection_v<field_t>)
                {
                    return;
                }
                else
                {
                    orm::db::binding::ObjectFieldToValues<field_t>::set(field, model, fieldIndex, values);
                }
            };

            orm::utils::constexpr_for_tuple(modelAsTuple, setObjectToValues);

            ind = i_ok;
        }
    };
}
} // namespace soci

namespace soci
{
export
{
    template <typename Owner, typename Target, typename SchemaType, bool JoinedValues>
    struct type_conversion<orm::db::binding::CollectionPayload<Owner, Target, SchemaType, JoinedValues>>
    {
        using base_type = values;

        [[maybe_unused]] static void
        from_base(const soci::values& values, indicator ind,
                  orm::db::binding::CollectionPayload<Owner, Target, SchemaType, JoinedValues>& payload)
        {
            orm::db::binding::BindingPayload<Target, SchemaType, JoinedValues> targetPayload{};
            type_conversion<orm::db::binding::BindingPayload<Target, SchemaType, JoinedValues>>::from_base(
                values, ind, targetPayload);
            payload.value = std::move(targetPayload.value);

            constexpr auto owner = orm::model::modelView<SchemaType, Owner>();
            const auto ownerColumns = orm::db::binding::getPrimaryKeyColumns(owner);
            payload.ownerKey.clear();
            payload.ownerKey.reserve(ownerColumns.size());

            for (const auto* column : ownerColumns)
            {
                const auto alias = orm::db::binding::relationOwnerAlias(*column);
                payload.ownerKey.push_back(orm::db::binding::getPrimaryKeyValue(values, alias, column->type.value()));
            }
        }

        [[maybe_unused]] static void
        to_base(const orm::db::binding::CollectionPayload<Owner, Target, SchemaType, JoinedValues>& /*payload*/,
                soci::values& /*values*/, indicator& ind)
        {
            ind = i_ok;
        }
    };
}
} // namespace soci

namespace soci
{
template <typename T>
using ProjectionPayload = orm::db::binding::ProjectionPayload<T>;

export
{
    template <typename T>
    struct type_conversion<ProjectionPayload<T>>
    {
        using base_type = values;

        [[maybe_unused]] static void from_base(const soci::values& values, indicator /*ind*/,
                                               ProjectionPayload<T>& payload)
        {
            auto resultAsTuple = orm::reflection::fieldPointers(payload.value);
            constexpr auto fields = orm::reflection::fields<T>();

            auto getObjectFromValues = [&fields, &values](auto index, auto* field)
            {
                using field_t = std::decay_t<decltype(*field)>;
                orm::db::binding::ObjectFieldFromProjectionValues<field_t>::get(field, std::string{fields[index].name},
                                                                                values);
            };

            orm::utils::constexpr_for_tuple(resultAsTuple, getObjectFromValues);
        }

        [[maybe_unused]] static void to_base(const ProjectionPayload<T>& /*payload*/, soci::values& /*values*/,
                                             indicator& ind)
        {
            ind = i_ok;
        }
    };
}
} // namespace soci

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
                                    std::span<const db::StatementParameter> parameters) -> void
{
    for (const auto& parameter : parameters)
    {
        bindStatementParameter(runtime, values, parameter);
    }
}

auto bindModelParameters(const db::BackendRuntime& runtime, soci::values& targetValues,
                         const soci::values& serializedModel, model::ModelView model) -> std::size_t;
auto normalizeAffectedRows(long long affectedRows) -> std::size_t;
[[nodiscard]] auto hasOwningJunction(model::ModelView owner) -> bool;
[[nodiscard]] auto requireCollectionRelation(model::ModelView owner,
                                             std::string_view fieldName) -> const model::RelationView*;
[[nodiscard]] auto requireCollectionTarget(model::ModelView owner, const model::RelationView& relation,
                                           model::TypeId expectedType) -> model::ModelView;
}
export
{
    // namespace detail

    /** A borrowed, schema-bound ORM view over an existing Database. */
    template <typename SchemaType>
    class OrmContext final
    {
        static_assert(requires { SchemaType::view; }, "OrmContext requires an orm::Schema<...> type");
        friend class Database;

        explicit OrmContext(Database& database) noexcept : database_{&database} {}
        Database* database_;

        template <typename T>
        static consteval auto requireSchemaModel() -> void
        {
            model::requireSchemaModel<SchemaType, T>();
        }

        template <typename Plan, typename... Args>
        auto selectPlanImpl(const Plan& plan, Args&&... args) -> std::vector<typename Plan::Result>
        {
            Plan::validateShape();
            query::detail::validateParameters<Plan, Args...>();
            if constexpr (query::detail::compiledSqlEligible<Plan>)
            {
                const auto values = std::forward_as_tuple(args...);
                const auto flavor = database_->getBackend().compiledSqlFlavor();
                if constexpr (query::detail::compiledStatement<SchemaType, Plan, db::CompiledSqlFlavor::SQLite>.valid)
                {
                    if (flavor == db::CompiledSqlFlavor::SQLite)
                        return executeCompiledPlan<Plan, db::CompiledSqlFlavor::SQLite>(plan, values);
                }
                if constexpr (
                    query::detail::compiledStatement<SchemaType, Plan, db::CompiledSqlFlavor::PostgreSQL>.valid)
                {
                    if (flavor == db::CompiledSqlFlavor::PostgreSQL)
                        return executeCompiledPlan<Plan, db::CompiledSqlFlavor::PostgreSQL>(plan, values);
                }
            }
            return executeDynamicPlan<>(plan, std::forward<Args>(args)...);
        }
        template <typename Plan, typename... Args>
        auto updatePlanImpl(const Plan& plan, Args&&... args) -> std::size_t
        {
            Plan::validateShape();
            query::detail::validateParameters<Plan, Args...>();
            if constexpr (query::detail::compiledSqlEligible<Plan>)
            {
                const auto values = std::forward_as_tuple(args...);
                const auto flavor = database_->getBackend().compiledSqlFlavor();
                if constexpr (query::detail::compiledStatement<SchemaType, Plan, db::CompiledSqlFlavor::SQLite>.valid)
                {
                    if (flavor == db::CompiledSqlFlavor::SQLite)
                        return executeCompiledPlan<Plan, db::CompiledSqlFlavor::SQLite>(plan, values);
                }
                if constexpr (
                    query::detail::compiledStatement<SchemaType, Plan, db::CompiledSqlFlavor::PostgreSQL>.valid)
                {
                    if (flavor == db::CompiledSqlFlavor::PostgreSQL)
                        return executeCompiledPlan<Plan, db::CompiledSqlFlavor::PostgreSQL>(plan, values);
                }
            }
            return executeDynamicPlan<>(plan, std::forward<Args>(args)...);
        }
        template <typename Plan, typename... Args>
        auto removePlanImpl(const Plan& plan, Args&&... args) -> std::size_t
        {
            Plan::validateShape();
            query::detail::validateParameters<Plan, Args...>();
            if constexpr (query::detail::compiledSqlEligible<Plan>)
            {
                const auto values = std::forward_as_tuple(args...);
                const auto flavor = database_->getBackend().compiledSqlFlavor();
                if constexpr (query::detail::compiledStatement<SchemaType, Plan, db::CompiledSqlFlavor::SQLite>.valid)
                {
                    if (flavor == db::CompiledSqlFlavor::SQLite)
                        return executeCompiledPlan<Plan, db::CompiledSqlFlavor::SQLite>(plan, values);
                }
                if constexpr (
                    query::detail::compiledStatement<SchemaType, Plan, db::CompiledSqlFlavor::PostgreSQL>.valid)
                {
                    if (flavor == db::CompiledSqlFlavor::PostgreSQL)
                        return executeCompiledPlan<Plan, db::CompiledSqlFlavor::PostgreSQL>(plan, values);
                }
            }
            return executeDynamicPlan<>(plan, std::forward<Args>(args)...);
        }

        template <typename Plan, typename... Args>
        auto executeDynamicPlan(const Plan& plan, Args&&... args)
        {
            auto bound = plan.toDynamic(std::forward<Args>(args)...);
            if constexpr (Plan::operation == query::detail::PlanOperation::Select)
                return selectImpl<>(bound);
            else if constexpr (Plan::operation == query::detail::PlanOperation::Update)
                return updateImpl<>(bound);
            else
                return removeImpl<typename Plan::Model>(query::detail::erase(bound));
        }

        template <typename Plan, db::CompiledSqlFlavor Flavor, typename Args>
        auto executeCompiledPlan(const Plan& plan, const Args& values)
        {
            constexpr auto descriptor = model::modelView<SchemaType, typename Plan::Model>();
            constexpr auto& compiled = query::detail::compiledStatement<SchemaType, Plan, Flavor>;
            const auto requirements = compiled.program.view();
            if constexpr (Plan::operation == query::detail::PlanOperation::Select)
            {
                if constexpr (Plan::isProjection)
                    detail::validateProjectionAliases<typename Plan::Result>(requirements.projections);
                database_->ensureQuerySupported(descriptor, requirements);
                const auto parameters = query::detail::collectParameters(plan, values);
                return executeSelectStatement<typename Plan::Model, typename Plan::Result, Plan::isProjection>(
                    db::StatementView{compiled.view(), parameters}, Plan::shouldJoin,
                    [&](auto& rows)
                    {
                        (void)rows;
                        if constexpr (!Plan::isProjection)
                            loadIncludedCollections<>(descriptor, requirements, rows);
                    });
            }
            else
            {
                constexpr auto operation =
                    Plan::operation == query::detail::PlanOperation::Update ? "update" : "remove";
                database_->ensureModelSupported(descriptor, operation);
                if constexpr (Plan::operation == query::detail::PlanOperation::Update)
                    database_->requireCapability(database_->getBackendCapabilities().mutations.update, operation,
                                                 "update is not supported");
                else
                    database_->requireCapability(database_->getBackendCapabilities().mutations.remove, operation,
                                                 "remove is not supported");
                database_->ensurePredicateSupported(
                    std::ranges::any_of(requirements.nodes, [](const auto& node)
                                        { return node.kind == db::detail::SqlNodeKind::Collection; }),
                    operation);
                const auto parameters = query::detail::collectParameters(plan, values);
                return database_->executeMutation(db::StatementView{compiled.view(), parameters}, operation);
            }
        }
        template <typename T>
        auto selectImpl(Query<T>& query) -> std::vector<T>
        {
            constexpr auto descriptor = model::modelView<SchemaType, T>();
            database_->ensureQuerySupported(descriptor, query.getData());
            const auto statement = database_->getCommandGenerator().select(descriptor, query.getData());
            return executeSelectStatement<T, T, false>(
                db::StatementView{statement.sql, statement.parameters}, query.getData().shouldJoin,
                [&](auto& rows) { loadIncludedCollections<>(descriptor, query.getData(), rows); });
        }

        template <typename Source, typename Result>
        auto selectImpl(ProjectionQuery<Source, Result>& query) -> std::vector<Result>
        {
            constexpr auto descriptor = model::modelView<SchemaType, Source>();
            database_->ensureQuerySupported(descriptor, query.getData());
            const auto statement = database_->getCommandGenerator().select(descriptor, query.getData());
            return executeSelectStatement<Source, Result, true>(db::StatementView{statement.sql, statement.parameters},
                                                                query.getData().shouldJoin, [](auto&) {});
        }

        template <typename Source, typename Result, bool Projection, typename Loader>
        auto executeSelectStatement(db::StatementView statement, bool shouldJoin,
                                    Loader loadIncludes) -> std::vector<Result>
        {
            constexpr auto operation = Projection ? "select projection" : "select";
            database_->ensureStatementWithinBindLimit(statement.parameters.size(), operation);
            std::vector<Result> result;
            try
            {
                soci::values parameterValues;
                detail::bindStatementParameters(database_->getBackend().runtime(), parameterValues,
                                                statement.parameters);
                if constexpr (Projection)
                {
                    soci::rowset<db::binding::ProjectionPayload<Result>> rows =
                        (database_->sql.prepare << statement.sql, soci::use(parameterValues));
                    for (auto& payload : rows)
                        result.push_back(std::move(payload.value));
                }
                else
                {
                    auto readRows = [&]<bool Joined>()
                    {
                        soci::rowset<db::binding::BindingPayload<Source, SchemaType, Joined>> rows =
                            (database_->sql.prepare << statement.sql, soci::use(parameterValues));
                        for (auto& payload : rows)
                            result.push_back(std::move(payload.value));
                    };
                    if (shouldJoin)
                        readRows.template operator()<true>();
                    else
                        readRows.template operator()<false>();
                }
                loadIncludes(result);
            }
            catch (const db::binding::ConversionError&)
            {
                throw DatabaseError{DatabaseErrorCode::Conversion, database_->backendType, operation,
                                    Projection ? "A database result cannot be represented by the requested projection" :
                                                 "A database result cannot be represented by the requested model"};
            }
            catch (const soci::soci_error& error)
            {
                database_->throwTranslatedError(error, DatabaseErrorCode::Statement, operation);
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
        auto insertImpl(const std::vector<T>& objects) -> void
        {
            for (const auto& object : objects)
            {
                insertImpl<>(object);
            }
        }

        /**
         * @brief Executes a insert query for a single object.
         *
         * @tparam T The type of the query.
         * @param object The object of type T to insert.
         */
        template <typename T>
        auto insertImpl(T object) -> void
        {
            constexpr auto model = model::modelView<SchemaType, T>();
            database_->ensureModelSupported(model, "insert");
            database_->requireCapability(database_->getBackendCapabilities().mutations.insert, "insert",
                                         "insert is not supported");
            const auto command = database_->getCommandGenerator().insert(model);

            const db::binding::BindingPayload<T, SchemaType> payload{};

            payload.value = std::move(object);

            try
            {
                soci::values serializedModel;
                auto indicator = soci::i_ok;
                soci::type_conversion<db::binding::BindingPayload<T, SchemaType>>::to_base(payload, serializedModel,
                                                                                           indicator);

                soci::values parameterValues;
                const auto parameterCount = detail::bindModelParameters(database_->getBackend().runtime(),
                                                                        parameterValues, serializedModel, model);
                database_->ensureStatementWithinBindLimit(parameterCount, "insert");

                if (parameterCount == 0)
                {
                    database_->sql << command;
                }
                else
                {
                    database_->sql << command, soci::use(parameterValues);
                }
            }
            catch (const db::binding::ConversionError&)
            {
                throw DatabaseError{DatabaseErrorCode::Conversion, database_->backendType, "insert",
                                    "A model value cannot be represented by the selected backend"};
            }
            catch (const soci::soci_error& error)
            {
                database_->throwTranslatedError(error, DatabaseErrorCode::Statement, "insert");
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
        auto updateImpl(const Update<T>& update) -> std::size_t
        {
            constexpr auto model = model::modelView<SchemaType, T>();
            database_->ensureModelSupported(model, "update");
            database_->requireCapability(database_->getBackendCapabilities().mutations.update, "update",
                                         "update is not supported");
            if (update.getData().predicate.has_value())
            {
                database_->ensurePredicateSupported(*update.getData().predicate, "update");
            }
            const auto statement = database_->getCommandGenerator().update(model, update.getData());

            return database_->executeMutation(statement, "update");
        }

        /**
         * @brief Executes a delete query for rows matching a predicate.
         *
         * @tparam T The type of the model whose rows will be deleted.
         * @param predicate The required predicate used in the WHERE clause.
         * @return The number of affected rows.
         */
        template <typename T>
        auto removeImpl(const query::detail::Predicate& predicate) -> std::size_t
        {
            constexpr auto model = model::modelView<SchemaType, T>();
            database_->ensureModelSupported(model, "remove");
            database_->requireCapability(database_->getBackendCapabilities().mutations.remove, "remove",
                                         "remove is not supported");
            database_->ensurePredicateSupported(predicate, "remove");
            const auto statement = database_->getCommandGenerator().remove(model, predicate);

            return database_->executeMutation(statement, "remove");
        }

        /**
         * @brief Execute a create table query for a model.
         *
         * @tparam T The type of the model which table to will be created.
         */
        template <typename T>
        auto createTableImpl() -> void
        {
            constexpr auto model = model::modelView<SchemaType, T>();
            database_->ensureModelSupported(model, "create table");
            database_->requireCapability(database_->getBackendCapabilities().schema.createTableIfNotExists,
                                         "create table", "idempotent table creation is not supported");
            const auto command = database_->getCommandGenerator().createTable(model);
            database_->executeSql(command, "create table");
        }

        /**
         * @brief Execute a delete table query for a model.
         *
         * @tparam T The type of the model which table to will be deleted.
         */
        template <typename T>
        auto deleteTableImpl() -> void
        {
            constexpr auto model = model::modelView<SchemaType, T>();
            database_->ensureModelSupported(model, "drop table");
            database_->requireCapability(database_->getBackendCapabilities().schema.dropTableIfExists, "drop table",
                                         "idempotent table removal is not supported");
            const auto command = database_->getCommandGenerator().dropTable(model);
            database_->executeSql(command, "drop table");
        }

        /**
         * @brief Creates junction tables owned by a model's ManyToMany mappings.
         *
         * Endpoint tables must already exist. Inverse mappings intentionally do
         * not create the shared junction table.
         */
        template <typename T>
        auto createRelationTablesImpl() -> void
        {
            const auto owner = model::modelView<SchemaType, T>();
            const auto ownsJunctionTable = detail::hasOwningJunction(owner);

            if (not ownsJunctionTable)
            {
                return;
            }

            database_->ensureModelSupported(owner, "create relation tables");
            database_->ensureRelationTableEndpointsExist(owner);

            for (const auto& command : db::relations::createTableStatements(database_->getBackend().dialect(), owner))
            {
                database_->executeSql(command, "create relation table");
            }
        }

        /**
         * @brief Drops junction tables owned by a model's ManyToMany mappings.
         */
        template <typename T>
        auto deleteRelationTablesImpl() -> void
        {
            const auto owner = model::modelView<SchemaType, T>();
            const auto ownsJunctionTable = detail::hasOwningJunction(owner);

            if (not ownsJunctionTable)
            {
                return;
            }

            database_->ensureModelSupported(owner, "drop relation tables");
            database_->requireCapability(database_->getBackendCapabilities().relations.junctionTables,
                                         "drop relation tables", "junction tables are not supported");
            database_->requireCapability(database_->getBackendCapabilities().schema.dropTableIfExists,
                                         "drop relation tables", "idempotent table removal is not supported");

            for (const auto& command : db::relations::dropTableStatements(database_->getBackend().dialect(), owner))
            {
                database_->executeSql(command, "drop relation table");
            }
        }

        /**
         * @brief Creates or changes a OneToMany/ManyToMany association.
         * @return 1 when database state changed, otherwise 0.
         */
        template <typename Owner, typename Target>
        auto linkImpl(const Owner& owner, std::string_view relationField, const Target& target) -> std::size_t
        {
            constexpr auto ownerDescriptor = model::modelView<SchemaType, Owner>();
            const auto* relation = detail::requireCollectionRelation(ownerDescriptor, relationField);
            const auto targetDescriptor =
                detail::requireCollectionTarget(ownerDescriptor, *relation, model::typeId<Target>());

            database_->ensureModelSupported(ownerDescriptor, "link relation");
            database_->ensureModelSupported(*targetDescriptor, "link relation");
            const auto ownerKey = db::binding::getPrimaryKey<SchemaType>(owner);
            const auto targetKey = db::binding::getPrimaryKey<SchemaType>(target);

            if (relation->kind == model::RelationKind::OneToMany)
            {
                database_->requireCapability(database_->getBackendCapabilities().relations.oneToMany, "link relation",
                                             "one-to-many relations are not supported");
            }
            else
            {
                database_->requireCapability(database_->getBackendCapabilities().relations.manyToMany, "link relation",
                                             "many-to-many relations are not supported");
                database_->requireCapability(database_->getBackendCapabilities().mutations.atomicInsertIfAbsent,
                                             "link relation", "idempotent relation links are not supported");
            }
            database_->requireCapability(relation->kind == model::RelationKind::OneToMany ?
                                             database_->getBackendCapabilities().mutations.update :
                                             database_->getBackendCapabilities().mutations.insert,
                                         "link relation", "relation mutations are not supported");

            if (not database_->relationEndpointExists(ownerDescriptor, ownerKey) or
                not database_->relationEndpointExists(*targetDescriptor, targetKey))
            {
                throw std::invalid_argument{"Cannot link relation endpoints that do not exist"};
            }

            return database_->executeMutation(db::relations::linkStatement(database_->getBackend().dialect(),
                                                                           ownerDescriptor, *relation, ownerKey,
                                                                           targetKey),
                                              "link relation");
        }

        /**
         * @brief Removes a OneToMany/ManyToMany association.
         * @return 1 when database state changed, otherwise 0.
         */
        template <typename Owner, typename Target>
        auto unlinkImpl(const Owner& owner, std::string_view relationField, const Target& target) -> std::size_t
        {
            constexpr auto ownerDescriptor = model::modelView<SchemaType, Owner>();
            const auto* relation = detail::requireCollectionRelation(ownerDescriptor, relationField);
            const auto targetDescriptor =
                detail::requireCollectionTarget(ownerDescriptor, *relation, model::typeId<Target>());

            database_->ensureModelSupported(ownerDescriptor, "unlink relation");
            database_->ensureModelSupported(*targetDescriptor, "unlink relation");
            database_->requireCapability(relation->kind == model::RelationKind::OneToMany ?
                                             database_->getBackendCapabilities().relations.oneToMany :
                                             database_->getBackendCapabilities().relations.manyToMany,
                                         "unlink relation", "the requested collection relation is not supported");
            database_->requireCapability(relation->kind == model::RelationKind::OneToMany ?
                                             database_->getBackendCapabilities().mutations.update :
                                             database_->getBackendCapabilities().mutations.remove,
                                         "unlink relation", "relation mutations are not supported");

            return database_->executeMutation(
                db::relations::unlinkStatement(database_->getBackend().dialect(), ownerDescriptor, *relation,
                                               db::binding::getPrimaryKey<SchemaType>(owner),
                                               db::binding::getPrimaryKey<SchemaType>(target)),
                "unlink relation");
        }

        template <typename Owner, typename Target, bool JoinedValues>
        auto appendCollectionRows(
            const db::Statement& statement,
            std::map<db::binding::PrimaryKey, std::vector<Target>, db::binding::PrimaryKeyLess>& groupedTargets) -> void
        {
            database_->ensureStatementWithinBindLimit(statement.parameters.size(), "include collection");
            soci::values parameterValues;
            detail::bindStatementParameters(database_->getBackend().runtime(), parameterValues, statement.parameters);
            soci::rowset<db::binding::CollectionPayload<Owner, Target, SchemaType, JoinedValues>> preparedRowSet =
                (database_->sql.prepare << statement.sql, soci::use(parameterValues));

            for (auto& payload : preparedRowSet)
            {
                groupedTargets[payload.ownerKey].push_back(std::move(payload.value));
            }
        }

        template <typename Owner, typename Options>
        auto loadIncludedCollections(model::ModelView ownerDescriptor, const Options& queryData,
                                     std::vector<Owner>& owners) -> void
        {
            if (queryData.includes.empty())
            {
                return;
            }

            for (const auto& includedRelation : queryData.includes)
            {
                (void)detail::requireCollectionRelation(ownerDescriptor, includedRelation);
            }

            if (owners.empty())
            {
                return;
            }

            constexpr auto reflectedFields = reflection::fields<Owner>();
            auto ownerFields = reflection::fieldPointers(owners.front());
            auto loadField =
                [this, &queryData, &owners, ownerDescriptor, &reflectedFields](auto fieldIndex, auto* field)
            {
                (void)this;
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

                    loadCollectionField<decltype(fieldIndex)::value, Owner, collection_t>(ownerDescriptor, queryData,
                                                                                          owners, *relation);
                }
            };

            utils::constexpr_for_tuple(ownerFields, loadField);
        }

        template <std::size_t FieldIndex, typename Owner, typename Collection, typename Options>
        auto loadCollectionField(model::ModelView ownerDescriptor, const Options& queryData, std::vector<Owner>& owners,
                                 const model::RelationView& relation) -> void
        {
            using target_t = orm::relation_target_t<Collection>;

            const auto targetDescriptor =
                detail::requireCollectionTarget(ownerDescriptor, relation, model::typeId<target_t>());

            const auto ownerPrimaryKey = db::binding::getPrimaryKeyColumns(ownerDescriptor);
            const auto runtimeLimits = database_->getBackendRuntimeLimits();
            const auto parameterBudget =
                runtimeLimits.maxBindParameters.value_or(std::numeric_limits<std::size_t>::max());

            if (parameterBudget < ownerPrimaryKey.size())
            {
                throw DatabaseError{DatabaseErrorCode::UnsupportedFeature, database_->backendType, "include collection",
                                    "The backend bind-parameter limit is too small for the relation primary key"};
            }

            const auto batchSize = runtimeLimits.maxBindParameters.has_value() ?
                                       std::max<std::size_t>(1, parameterBudget / ownerPrimaryKey.size()) :
                                       owners.size();
            std::map<db::binding::PrimaryKey, std::vector<target_t>, db::binding::PrimaryKeyLess> groupedTargets;

            orm::Query<target_t> targetQuery;

            if (not queryData.shouldJoin)
            {
                targetQuery.disableJoining();
            }

            const auto baseTargetStatement =
                database_->getCommandGenerator().select(*targetDescriptor, targetQuery.getData());

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

                const auto statement = db::relations::collectionSelectStatement(
                    database_->getBackend().dialect(), ownerDescriptor, relation, baseTargetStatement.sql, ownerKeys,
                    queryData.shouldJoin);
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

    public:
        OrmContext(const OrmContext&) noexcept = default;
        OrmContext(OrmContext&&) noexcept = default;
        auto operator=(const OrmContext&) noexcept -> OrmContext& = default;
        auto operator=(OrmContext&&) noexcept -> OrmContext& = default;

        template <query::detail::StaticPlanType Plan, typename... Args>
            requires(Plan::operation == query::detail::PlanOperation::Select)
        auto select(const Plan& plan, Args&&... args) -> std::vector<typename Plan::Result>
        {
            requireSchemaModel<typename Plan::Model>();
            return this->template selectPlanImpl<>(plan, std::forward<Args>(args)...);
        }
        template <query::detail::StaticPlanType Plan, typename... Args>
            requires(Plan::operation == query::detail::PlanOperation::Update)
        auto update(const Plan& plan, Args&&... args) -> std::size_t
        {
            requireSchemaModel<typename Plan::Model>();
            return this->template updatePlanImpl<>(plan, std::forward<Args>(args)...);
        }
        template <query::detail::StaticPlanType Plan, typename... Args>
            requires(Plan::operation == query::detail::PlanOperation::Remove)
        auto remove(const Plan& plan, Args&&... args) -> std::size_t
        {
            requireSchemaModel<typename Plan::Model>();
            return this->template removePlanImpl<>(plan, std::forward<Args>(args)...);
        }

        template <typename T>
        using Payload = db::binding::BindingPayload<T, SchemaType>;

        template <typename T>
        using ProjectionPayload = db::binding::ProjectionPayload<T>;

        template <typename T>
        auto select(Query<T>& query) -> std::vector<T>
        {
            requireSchemaModel<T>();
            return this->template selectImpl<>(query);
        }

        template <typename Source, typename Result>
        auto select(ProjectionQuery<Source, Result>& query) -> std::vector<Result>
        {
            requireSchemaModel<Source>();
            return this->template selectImpl<>(query);
        }

        template <typename T>
        auto insert(const std::vector<T>& objects) -> void
        {
            requireSchemaModel<T>();
            this->template insertImpl<>(objects);
        }

        template <typename T>
        auto insert(T object) -> void
        {
            requireSchemaModel<T>();
            this->template insertImpl<>(std::move(object));
        }

        template <typename T>
        auto update(const Update<T>& update) -> std::size_t
        {
            requireSchemaModel<T>();
            return this->template updateImpl<>(update);
        }

        template <typename T, typename P>
            requires query::detail::PredicateFor<P, T> && query::detail::ORM_QUERY_WRITE_SAFE<P>
        auto remove(const P& predicate) -> std::size_t
        {
            requireSchemaModel<T>();
            return this->template removeImpl<T>(query::detail::erase(predicate));
        }

        template <typename T>
        auto createTable() -> void
        {
            requireSchemaModel<T>();
            this->template createTableImpl<T>();
        }

        template <typename T>
        auto deleteTable() -> void
        {
            requireSchemaModel<T>();
            this->template deleteTableImpl<T>();
        }

        template <typename T>
        auto createRelationTables() -> void
        {
            requireSchemaModel<T>();
            this->template createRelationTablesImpl<T>();
        }

        template <typename T>
        auto deleteRelationTables() -> void
        {
            requireSchemaModel<T>();
            this->template deleteRelationTablesImpl<T>();
        }

        template <auto Member, typename Owner, typename Target>
            requires query::detail::ORM_QUERY_COLLECTION<Member> &&
                         query::detail::ORM_QUERY_MODEL_TYPE<Owner,
                                                             typename query::detail::CollectionTraits<Member>::Model> &&
                         query::detail::ORM_QUERY_MODEL_TYPE<Target,
                                                             typename query::detail::CollectionTraits<Member>::Target>
        auto link(const Owner& owner, const Target& target) -> std::size_t
        {
            requireSchemaModel<Owner>();
            requireSchemaModel<Target>();
            return this->template linkImpl<>(owner, query::detail::CollectionTraits<Member>::name(), target);
        }

        template <auto Member, typename Owner, typename Target>
            requires query::detail::ORM_QUERY_COLLECTION<Member> &&
                         query::detail::ORM_QUERY_MODEL_TYPE<Owner,
                                                             typename query::detail::CollectionTraits<Member>::Model> &&
                         query::detail::ORM_QUERY_MODEL_TYPE<Target,
                                                             typename query::detail::CollectionTraits<Member>::Target>
        auto unlink(const Owner& owner, const Target& target) -> std::size_t
        {
            requireSchemaModel<Owner>();
            requireSchemaModel<Target>();
            return this->template unlinkImpl<>(owner, query::detail::CollectionTraits<Member>::name(), target);
        }
    };
}

} // namespace orm
