#include "orm-cxx/database.hpp"

#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#include "orm-cxx/database/BackendRuntime.hpp"
#include "orm-cxx/database/SqlDialect.hpp"

namespace
{
auto containsCollectionPredicate(const orm::query::PredicateNode& node) -> bool;

auto serializedBoundValue(const soci::values& values, const std::string& name, orm::model::ColumnType type)
    -> orm::db::BoundValue
{
    if (values.get_indicator(name) == soci::i_null)
    {
        return orm::db::BoundValue{.logicalType = type, .value = std::nullopt};
    }

    auto makeValue = [type, &name](orm::query::QueryValue::Value value)
    {
        if (not orm::query::QueryValue::isCompatibleStorage(type, value))
        {
            throw orm::db::binding::ConversionError{"Serialized model value does not match its logical column type: " +
                                                    name};
        }

        return orm::db::BoundValue{.logicalType = type, .value = std::move(value)};
    };

    switch (type)
    {
    case orm::model::ColumnType::Bool:
    case orm::model::ColumnType::Char:
    case orm::model::ColumnType::UnsignedChar:
    case orm::model::ColumnType::Short:
    case orm::model::ColumnType::UnsignedShort:
    case orm::model::ColumnType::Int:
        return makeValue(values.get<int>(name));
    case orm::model::ColumnType::UnsignedInt:
    case orm::model::ColumnType::UnsignedLongLong:
        return makeValue(values.get<unsigned long long>(name));
    case orm::model::ColumnType::LongLong:
        return makeValue(values.get<long long>(name));
    case orm::model::ColumnType::Float:
    case orm::model::ColumnType::Double:
        return makeValue(values.get<double>(name));
    case orm::model::ColumnType::String:
        return makeValue(values.get<std::string>(name));
    case orm::model::ColumnType::Uuid:
    case orm::model::ColumnType::Unknown:
    case orm::model::ColumnType::OneToOne:
        break;
    }

    throw std::invalid_argument{"Cannot bind a serialized model value with an unsupported column type: " + name};
}

auto containsCollectionPredicate(const orm::query::PredicateNode& node) -> bool
{
    return std::visit(
        [](const auto& expression) -> bool
        {
            using expression_t = std::decay_t<decltype(expression)>;

            if constexpr (std::is_same_v<expression_t, orm::query::CollectionExpression>)
            {
                return true;
            }
            else if constexpr (std::is_same_v<expression_t, orm::query::LogicalExpression>)
            {
                return containsCollectionPredicate(*expression.left) or containsCollectionPredicate(*expression.right);
            }
            else if constexpr (std::is_same_v<expression_t, orm::query::NotExpression>)
            {
                return containsCollectionPredicate(*expression.predicate);
            }
            else
            {
                return false;
            }
        },
        node.expression);
}
} // namespace

namespace orm
{
auto detail::bindModelParameters(const db::BackendRuntime& runtime, soci::values& targetValues,
                                 const soci::values& serializedModel, const model::ModelInfo& modelInfo) -> std::size_t
{
    std::size_t parameterCount{};

    for (const auto& column : modelInfo.columnsInfo)
    {
        if (column.isAutoIncrement)
        {
            continue;
        }

        if (column.isForeignModel)
        {
            const auto& relatedModel = modelInfo.foreignModelsInfo.at(column.name);

            for (const auto& relatedColumn : relatedModel.columnsInfo)
            {
                if (not relatedColumn.isPrimaryKey)
                {
                    continue;
                }

                const auto parameterName = std::format("{}_{}", column.name, relatedColumn.name);
                runtime.bind(targetValues, parameterName,
                             serializedBoundValue(serializedModel, parameterName, relatedColumn.type));
                ++parameterCount;
            }

            continue;
        }

        runtime.bind(targetValues, column.name, serializedBoundValue(serializedModel, column.name, column.type));
        ++parameterCount;
    }

    return parameterCount;
}

auto detail::normalizeAffectedRows(long long affectedRows) -> std::size_t
{
    if (affectedRows < 0)
    {
        throw std::runtime_error{"Database backend did not report affected row count"};
    }

    return static_cast<std::size_t>(affectedRows);
}

Database::Database() : backendType{db::BackendType::Empty} {}

Database::Database(db::CommandGeneratorFactory factory)
    : backendType{db::BackendType::Empty}, commandGeneratorFactory{std::move(factory)}
{
}

auto Database::connect(const std::string& connectionString) -> void
{
    if (backend != nullptr)
    {
        throw DatabaseError{DatabaseErrorCode::AlreadyConnected, backendType, "connect",
                            "A database session is already connected"};
    }

    const auto* selectedBackend = commandGeneratorFactory.findBackend(connectionString);

    if (selectedBackend == nullptr)
    {
        throw DatabaseError{DatabaseErrorCode::UnsupportedBackend, db::BackendType::Empty, "connect",
                            "No unique registered backend accepts the connection string"};
    }

    connect(selectedBackend->type(), connectionString);
}

auto Database::connect(db::BackendType requestedBackend, const std::string& connectionString) -> void
{
    if (backend != nullptr)
    {
        throw DatabaseError{DatabaseErrorCode::AlreadyConnected, backendType, "connect",
                            "A database session is already connected"};
    }

    const db::BackendProvider* selectedBackend{};

    try
    {
        selectedBackend = &commandGeneratorFactory.getBackend(requestedBackend);
    }
    catch (const std::out_of_range&)
    {
        throw DatabaseError{DatabaseErrorCode::UnsupportedBackend, requestedBackend, "connect",
                            "The requested backend is not registered"};
    }

    if (not selectedBackend->acceptsConnectionString(connectionString))
    {
        throw DatabaseError{DatabaseErrorCode::Connection, requestedBackend, "connect",
                            "The connection string is not valid for the requested backend"};
    }

    auto closeFailedSession = [this]() noexcept
    {
        if (not sql.is_connected())
        {
            return;
        }

        sql.close();
    };

    try
    {
        sql.open(connectionString);
        selectedBackend->runtime().onConnect(sql);
    }
    catch (const soci::soci_error& error)
    {
        closeFailedSession();
        throw selectedBackend->runtime().translateError(error, DatabaseErrorCode::Connection, "connect");
    }
    catch (const std::exception&)
    {
        closeFailedSession();
        throw DatabaseError{DatabaseErrorCode::Connection, requestedBackend, "connect",
                            "Backend session initialization failed"};
    }
    catch (...)
    {
        closeFailedSession();
        throw DatabaseError{DatabaseErrorCode::Connection, requestedBackend, "connect",
                            "Backend session initialization failed"};
    }

    backend = selectedBackend;
    backendType = requestedBackend;
}

auto Database::disconnect() -> void
{
    if (backend == nullptr)
    {
        backendType = db::BackendType::Empty;
        return;
    }

    const auto* connectedBackend = backend;
    std::optional<DatabaseError> failure;

    if (transaction != nullptr)
    {
        try
        {
            transaction->rollback();
        }
        catch (const soci::soci_error& error)
        {
            failure = connectedBackend->runtime().translateError(error, DatabaseErrorCode::Transaction,
                                                                 "rollback during disconnect");
        }

        transaction.reset();
    }

    sql.close();

    backend = nullptr;
    backendType = db::BackendType::Empty;

    if (failure.has_value())
    {
        throw failure.value();
    }
}

auto Database::getBackendType() const noexcept -> db::BackendType
{
    return backendType;
}

auto Database::isConnected() const noexcept -> bool
{
    return backend != nullptr and sql.is_connected();
}

auto Database::getBackendCapabilities() const -> const db::BackendCapabilities&
{
    return getBackend().capabilities();
}

auto Database::beginTransaction() -> void
{
    const auto& capabilities = getBackendCapabilities();
    requireCapability(capabilities.transactions, "begin transaction", "transactions are not supported");

    if (transaction != nullptr)
    {
        throw DatabaseError{DatabaseErrorCode::Transaction, backendType, "begin transaction",
                            "A transaction is already active"};
    }

    try
    {
        transaction = std::make_unique<soci::transaction>(sql);
    }
    catch (const soci::soci_error& error)
    {
        throwTranslatedError(error, DatabaseErrorCode::Transaction, "begin transaction");
    }
}

auto Database::commitTransaction() -> void
{
    (void)getBackend();

    if (transaction == nullptr)
    {
        throw DatabaseError{DatabaseErrorCode::Transaction, backendType, "commit transaction",
                            "No transaction is active"};
    }

    try
    {
        transaction->commit();
        transaction.reset();
    }
    catch (const soci::soci_error& error)
    {
        transaction.reset();
        throwTranslatedError(error, DatabaseErrorCode::Transaction, "commit transaction");
    }
}

auto Database::rollbackTransaction() -> void
{
    (void)getBackend();

    if (transaction == nullptr)
    {
        throw DatabaseError{DatabaseErrorCode::Transaction, backendType, "rollback transaction",
                            "No transaction is active"};
    }

    try
    {
        transaction->rollback();
        transaction.reset();
    }
    catch (const soci::soci_error& error)
    {
        transaction.reset();
        throwTranslatedError(error, DatabaseErrorCode::Transaction, "rollback transaction");
    }
}

auto Database::executeMutation(const db::Statement& statement, std::string_view operation) -> std::size_t
{
    ensureAffectedRowsAvailable(operation);
    soci::values parameterValues;

    try
    {
        detail::bindStatementParameters(getBackend().runtime(), parameterValues, statement.parameters);

        auto executeAndGetAffectedRows = [this](soci::statement& preparedStatement) -> std::size_t
        {
            preparedStatement.execute(true);

            return getBackend().runtime().normalizeAffectedRows(preparedStatement.get_affected_rows());
        };

        if (statement.parameters.empty())
        {
            soci::statement preparedStatement = (sql.prepare << statement.sql);

            return executeAndGetAffectedRows(preparedStatement);
        }

        soci::statement preparedStatement = (sql.prepare << statement.sql, soci::use(parameterValues));

        return executeAndGetAffectedRows(preparedStatement);
    }
    catch (const DatabaseError&)
    {
        throw;
    }
    catch (const db::binding::ConversionError&)
    {
        throw DatabaseError{DatabaseErrorCode::Conversion, backendType, std::string{operation},
                            "A mutation value cannot be represented by the selected backend"};
    }
    catch (const soci::soci_error& error)
    {
        throwTranslatedError(error, DatabaseErrorCode::Statement, operation);
    }
    catch (const std::exception&)
    {
        throw DatabaseError{DatabaseErrorCode::AffectedRowsUnavailable, backendType, std::string{operation},
                            "The backend did not provide a usable affected-row count"};
    }
}

auto Database::executeSql(std::string_view statement, std::string_view operation) -> void
{
    (void)getBackend();

    try
    {
        sql << std::string{statement};
    }
    catch (const soci::soci_error& error)
    {
        throwTranslatedError(error, DatabaseErrorCode::Statement, operation);
    }
}

auto Database::relationEndpointExists(const model::ModelInfo& modelInfo, const db::binding::PrimaryKey& key) -> bool
{
    const auto primaryKeyColumns = db::binding::getPrimaryKeyColumns(modelInfo);

    if (primaryKeyColumns.size() != key.size())
    {
        throw std::invalid_argument{"Incomplete relation endpoint primary key"};
    }

    const auto& dialect = getBackend().dialect();
    db::Statement statement;
    std::string where;

    for (std::size_t i = 0; i < primaryKeyColumns.size(); ++i)
    {
        if (not where.empty())
        {
            where += " AND ";
        }

        const auto parameterName = std::format("orm_endpoint_{}", i);
        where += std::format("{} = {}", dialect.quoteIdentifier(primaryKeyColumns[i]->name),
                             dialect.bindMarker(parameterName));
        statement.parameters.push_back(
            db::StatementParameter{.name = parameterName, .value = db::binding::toQueryValue(key[i])});
    }

    statement.sql =
        std::format("SELECT COUNT(*) FROM {} WHERE {};", dialect.quoteIdentifier(modelInfo.tableName), where);
    soci::values parameterValues;
    long long count{};

    try
    {
        detail::bindStatementParameters(getBackend().runtime(), parameterValues, statement.parameters);
        sql << statement.sql, soci::use(parameterValues), soci::into(count);
    }
    catch (const db::binding::ConversionError&)
    {
        throw DatabaseError{DatabaseErrorCode::Conversion, backendType, "validate relation endpoint",
                            "A relation key cannot be represented by the selected backend"};
    }
    catch (const soci::soci_error& error)
    {
        throwTranslatedError(error, DatabaseErrorCode::Statement, "validate relation endpoint");
    }

    return count > 0;
}

auto Database::tableExists(std::string_view tableName) -> bool
{
    try
    {
        return getBackend().runtime().tableExists(sql, tableName);
    }
    catch (const soci::soci_error& error)
    {
        throwTranslatedError(error, DatabaseErrorCode::Statement, "inspect schema");
    }
}

auto Database::ensureRelationTableEndpointsExist(const model::ModelInfo& ownerInfo) -> void
{
    const auto& capabilities = getBackendCapabilities();
    requireCapability(capabilities.schema.createTableIfNotExists, "create relation tables",
                      "idempotent table creation is not supported");
    requireCapability(capabilities.schema.compositePrimaryKeys, "create relation tables",
                      "composite primary keys are not supported");
    requireCapability(capabilities.schema.foreignKeys, "create relation tables", "foreign keys are not supported");
    requireCapability(capabilities.schema.onDeleteCascade, "create relation tables",
                      "cascading foreign keys are not supported");
    requireCapability(capabilities.relations.junctionTables, "create relation tables",
                      "junction tables are not supported");
    requireCapability(capabilities.relations.manyToMany, "create relation tables",
                      "many-to-many relations are not supported");

    for (const auto& relation : ownerInfo.relationsInfo)
    {
        if (relation.kind != model::RelationKind::ManyToMany or not relation.junction.has_value() or
            not relation.junction->owningSide)
        {
            continue;
        }

        const auto ownerPrimaryKey = db::binding::getPrimaryKeyColumns(ownerInfo);
        const auto targetPrimaryKey = db::binding::getPrimaryKeyColumns(relation.targetModel());

        if (ownerPrimaryKey.size() > 1 or targetPrimaryKey.size() > 1)
        {
            requireCapability(capabilities.relations.compositeEndpointKeys, "create relation tables",
                              "composite relation endpoint keys are not supported");
        }

        for (const auto* column : ownerPrimaryKey)
        {
            if (not capabilities.supportsColumnType(column->type))
            {
                throw DatabaseError{DatabaseErrorCode::UnsupportedFeature, backendType, "create relation tables",
                                    "An owner primary-key type is not supported by the backend"};
            }
        }

        for (const auto* column : targetPrimaryKey)
        {
            if (not capabilities.supportsColumnType(column->type))
            {
                throw DatabaseError{DatabaseErrorCode::UnsupportedFeature, backendType, "create relation tables",
                                    "A target primary-key type is not supported by the backend"};
            }
        }

        if (not tableExists(ownerInfo.tableName) or not tableExists(relation.targetModel().tableName))
        {
            throw std::invalid_argument{"ManyToMany endpoint tables must exist before creating relation tables"};
        }
    }
}

auto Database::getBackend() const -> const db::BackendProvider&
{
    if (backend == nullptr or not sql.is_connected())
    {
        throw DatabaseError{DatabaseErrorCode::NotConnected, db::BackendType::Empty, "database operation",
                            "No database session is connected"};
    }

    return *backend;
}

auto Database::getCommandGenerator() const -> const db::CommandGenerator&
{
    return getBackend().commandGenerator();
}

auto Database::getBackendRuntimeLimits() -> db::BackendRuntimeLimits
{
    try
    {
        return getBackend().runtime().limits(sql);
    }
    catch (const soci::soci_error& error)
    {
        throwTranslatedError(error, DatabaseErrorCode::Statement, "read backend limits");
    }
}

auto Database::ensureModelSupported(const model::ModelInfo& modelInfo, std::string_view operation) const -> void
{
    const auto& capabilities = getBackendCapabilities();

    if (operation == "create table")
    {
        if (modelInfo.idColumnsNames.size() > 1)
        {
            requireCapability(capabilities.schema.compositePrimaryKeys, operation,
                              "composite primary keys are not supported");
        }

        if (not modelInfo.foreignModelsInfo.empty())
        {
            requireCapability(capabilities.schema.foreignKeys, operation, "foreign keys are not supported");
        }
    }

    for (const auto& column : modelInfo.columnsInfo)
    {
        if (column.isAutoIncrement and operation == "create table")
        {
            requireCapability(capabilities.schema.autoIncrementPrimaryKey, operation,
                              "auto-increment primary keys are not supported");
        }

        if (column.isForeignModel)
        {
            requireCapability(capabilities.relations.toOne, operation, "to-one relations are not supported");
            const auto& relatedModel = modelInfo.foreignModelsInfo.at(column.name);

            for (const auto& relatedColumn : relatedModel.columnsInfo)
            {
                if (relatedColumn.isPrimaryKey and not capabilities.supportsColumnType(relatedColumn.type))
                {
                    throw DatabaseError{DatabaseErrorCode::UnsupportedFeature, backendType, std::string{operation},
                                        "A related primary-key column type is not supported by the backend"};
                }
            }

            continue;
        }

        if (not capabilities.supportsColumnType(column.type))
        {
            throw DatabaseError{DatabaseErrorCode::UnsupportedFeature, backendType, std::string{operation},
                                "A model column type is not supported by the backend"};
        }
    }
}

auto Database::ensureQuerySupported(const query::QueryData& queryData) const -> void
{
    ensureModelSupported(queryData.modelInfo, "select");
    const auto& capabilities = getBackendCapabilities();

    if (queryData.limit.has_value())
    {
        requireCapability(capabilities.query.limit, "select", "LIMIT is not supported");
    }

    if (queryData.offset.has_value())
    {
        requireCapability(capabilities.query.offset, "select", "OFFSET is not supported");

        if (not queryData.limit.has_value())
        {
            requireCapability(capabilities.query.offsetWithoutLimit, "select", "OFFSET without LIMIT is not supported");
        }

        if (capabilities.query.offsetRequiresOrderBy and queryData.orderBy.empty())
        {
            throw DatabaseError{DatabaseErrorCode::UnsupportedFeature, backendType, "select",
                                "This backend requires ORDER BY when OFFSET is used"};
        }
    }

    if (not queryData.projections.empty())
    {
        requireCapability(capabilities.query.projections, "select projection", "projections are not supported");
    }

    if (not queryData.groupBy.empty())
    {
        requireCapability(capabilities.query.groupBy, "select", "GROUP BY is not supported");
    }

    if (queryData.having.has_value())
    {
        requireCapability(capabilities.query.having, "select", "HAVING is not supported");
    }

    if (queryData.predicate.has_value() and containsCollectionPredicate(queryData.predicate->getNode()))
    {
        requireCapability(capabilities.query.collectionPredicates, "select", "collection predicates are not supported");
        requireCapability(capabilities.relations.collectionPredicates, "select",
                          "collection predicates are not supported");
    }

    if (not queryData.includes.empty())
    {
        requireCapability(capabilities.relations.collectionIncludes, "include collection",
                          "collection includes are not supported");

        for (const auto& include : queryData.includes)
        {
            const auto* relation = queryData.modelInfo.findRelation(include);

            if (relation == nullptr)
            {
                continue;
            }

            ensureModelSupported(relation->targetModel(), "include collection");

            if (relation->kind == model::RelationKind::OneToMany)
            {
                requireCapability(capabilities.relations.oneToMany, "include collection",
                                  "one-to-many relations are not supported");
            }
            else if (relation->kind == model::RelationKind::ManyToMany)
            {
                requireCapability(capabilities.relations.manyToMany, "include collection",
                                  "many-to-many relations are not supported");
            }
        }
    }
}

auto Database::ensureAffectedRowsAvailable(std::string_view operation) const -> void
{
    if (getBackendCapabilities().mutations.affectedRows != db::AffectedRowsSupport::Reliable)
    {
        throw DatabaseError{DatabaseErrorCode::AffectedRowsUnavailable, backendType, std::string{operation},
                            "The backend cannot report exact affected-row counts"};
    }
}

auto Database::requireCapability(bool supported, std::string_view operation, std::string_view message) const -> void
{
    if (not supported)
    {
        throw DatabaseError{DatabaseErrorCode::UnsupportedFeature, backendType, std::string{operation},
                            std::string{message}};
    }
}

auto Database::throwTranslatedError(const soci::soci_error& error, DatabaseErrorCode fallback,
                                    std::string_view operation) const -> void
{
    if (backend == nullptr)
    {
        throw DatabaseError{DatabaseErrorCode::NotConnected, db::BackendType::Empty, "database operation",
                            "No database backend is available to translate the driver error"};
    }

    throw backend->runtime().translateError(error, fallback, operation);
}
} // namespace orm
