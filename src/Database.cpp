#include "orm-cxx/database.hpp"

#include <algorithm>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>

#include "database/defaults/SqlAliases.hpp"
#include "database/defaults/SqlRenderer.hpp"
#include "orm-cxx/database/BackendRuntime.hpp"
#include "orm-cxx/database/SqlDialect.hpp"

namespace
{
auto containsCollectionPredicate(const orm::query::PredicateNode& node) -> bool;

auto renderColumnForValidation(const orm::query::Column& column, orm::model::ModelView model,
                               const orm::query::SelectSpec& spec, const orm::db::SqlDialect& dialect) -> std::string
{
    const orm::db::commands::RenderContext context{
        .model = model,
        .dialect = dialect,
        .shouldJoin = spec.shouldJoin,
    };

    return orm::db::commands::renderColumn(column, context);
}

auto serializedBoundValue(const soci::values& values, const std::string& name,
                          orm::model::ColumnType type) -> orm::db::BoundValue
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

auto validateModelIdentifiers(const orm::db::SqlDialect& dialect, const orm::model::ModelView& model,
                              std::unordered_set<std::size_t>& visited) -> void
{
    if (not visited.insert(model.modelIndex).second)
    {
        return;
    }

    (void)dialect.quoteIdentifier(model->tableName);

    for (const auto& column : model->columns)
    {
        (void)dialect.quoteIdentifier(column.name);

        if (column.kind == orm::model::FieldKind::Scalar)
        {
            (void)dialect.quoteIdentifier(orm::db::aliases::modelColumn(model->tableName, column.name));

            if (column.isPrimaryKey)
            {
                (void)dialect.quoteIdentifier(orm::db::binding::relationOwnerAlias(column));
            }

            if (not column.isAutoIncrement)
            {
                (void)dialect.bindMarker(column.name);
            }

            continue;
        }

        const auto relatedModel = model.resolveTarget(column);
        if (relatedModel == nullptr)
        {
            throw std::invalid_argument{"To-one column has no target model in the schema"};
        }
        (void)dialect.quoteIdentifier(relatedModel->tableName);

        for (const auto& relatedColumn : relatedModel->columns)
        {
            (void)dialect.quoteIdentifier(relatedColumn.name);
            (void)dialect.quoteIdentifier(orm::db::aliases::joinedRelationColumn(column.name, relatedColumn.name));

            if (relatedColumn.isPrimaryKey)
            {
                const auto localColumn = orm::db::aliases::joinedRelationColumn(column.name, relatedColumn.name);
                (void)dialect.quoteIdentifier(localColumn);
                (void)dialect.bindMarker(localColumn);
                (void)dialect.quoteIdentifier(
                    orm::db::aliases::unjoinedRelationColumn(model->tableName, column.name, relatedColumn.name));
            }
        }

        validateModelIdentifiers(dialect, *relatedModel, visited);
    }

    for (const auto& relation : model->relations)
    {
        (void)dialect.quoteIdentifier(relation.columnName);
        const auto targetModel = model.resolveTarget(relation);
        if (targetModel == nullptr)
        {
            throw std::invalid_argument{"Collection relation has no target model in the schema"};
        }
        (void)dialect.quoteIdentifier(targetModel->tableName);

        for (const auto& targetColumn : targetModel->columns)
        {
            (void)dialect.quoteIdentifier(targetColumn.name);

            if (targetColumn.isPrimaryKey)
            {
                (void)dialect.quoteIdentifier(
                    orm::db::aliases::joinedRelationColumn(relation.columnName, targetColumn.name));
            }
        }

        if (relation.junction.isConfigured())
        {
            const auto& junction = relation.junction;
            (void)dialect.quoteIdentifier(junction.tableName);

            for (const auto& column : junction.ownerColumns)
            {
                (void)dialect.quoteIdentifier(column);
            }

            for (const auto& column : junction.targetColumns)
            {
                (void)dialect.quoteIdentifier(column);
            }
        }

        validateModelIdentifiers(dialect, *targetModel, visited);
    }
}

auto validateModelIdentifiers(const orm::db::SqlDialect& dialect, const orm::model::ModelView& model) -> void
{
    std::unordered_set<std::size_t> visited;
    validateModelIdentifiers(dialect, model, visited);
}
} // namespace

namespace orm
{
auto detail::bindModelParameters(const db::BackendRuntime& runtime, soci::values& targetValues,
                                 const soci::values& serializedModel, model::ModelView descriptor) -> std::size_t
{
    std::size_t parameterCount{};

    for (const auto& column : descriptor->columns)
    {
        if (column.isAutoIncrement)
        {
            continue;
        }

        if (column.kind == model::FieldKind::ToOne)
        {
            const auto relatedModel = descriptor.resolveTarget(column);
            if (relatedModel == nullptr)
            {
                throw std::invalid_argument{"To-one column has no target model in the schema"};
            }

            for (const auto& relatedColumn : relatedModel->columns)
            {
                if (not relatedColumn.isPrimaryKey)
                {
                    continue;
                }

                const auto parameterName = std::format("{}_{}", column.name, relatedColumn.name);
                runtime.bind(targetValues, parameterName,
                             serializedBoundValue(serializedModel, parameterName, relatedColumn.type.value()));
                ++parameterCount;
            }

            continue;
        }

        runtime.bind(targetValues, column.name,
                     serializedBoundValue(serializedModel, std::string{column.name}, column.type.value()));
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

DatabaseCore::DatabaseCore() : backendType{db::BackendType::Empty} {}

DatabaseCore::DatabaseCore(db::CommandGeneratorFactory factory)
    : backendType{db::BackendType::Empty}, commandGeneratorFactory{std::move(factory)}
{
}

auto DatabaseCore::connect(const std::string& connectionString) -> void
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

auto DatabaseCore::connect(db::BackendType requestedBackend, const std::string& connectionString) -> void
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
        selectedBackend->runtime().open(sql, connectionString);
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

auto DatabaseCore::disconnect() -> void
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
        transactionFailed = false;
    }

    sql.close();

    backend = nullptr;
    backendType = db::BackendType::Empty;
    transactionFailed = false;

    if (failure.has_value())
    {
        throw failure.value();
    }
}

auto DatabaseCore::getBackendType() const noexcept -> db::BackendType
{
    return backendType;
}

auto DatabaseCore::isConnected() const noexcept -> bool
{
    return backend != nullptr and sql.is_connected();
}

auto DatabaseCore::getBackendCapabilities() const -> const db::BackendCapabilities&
{
    return getBackend().capabilities();
}

auto DatabaseCore::beginTransaction() -> void
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
        transactionFailed = false;
    }
    catch (const soci::soci_error& error)
    {
        throwTranslatedError(error, DatabaseErrorCode::Transaction, "begin transaction");
    }
}

auto DatabaseCore::commitTransaction() -> void
{
    (void)getBackend();

    if (transaction == nullptr)
    {
        throw DatabaseError{DatabaseErrorCode::Transaction, backendType, "commit transaction",
                            "No transaction is active"};
    }

    if (transactionFailed)
    {
        throw DatabaseError{DatabaseErrorCode::Transaction, backendType, "commit transaction",
                            "The transaction contains a failed statement and must be rolled back"};
    }

    try
    {
        transaction->commit();
        transaction.reset();
        transactionFailed = false;
    }
    catch (const soci::soci_error& error)
    {
        transaction.reset();
        transactionFailed = false;
        throwTranslatedError(error, DatabaseErrorCode::Transaction, "commit transaction");
    }
}

auto DatabaseCore::rollbackTransaction() -> void
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
        transactionFailed = false;
    }
    catch (const soci::soci_error& error)
    {
        transaction.reset();
        transactionFailed = false;
        throwTranslatedError(error, DatabaseErrorCode::Transaction, "rollback transaction");
    }
}

auto DatabaseCore::executeMutation(const db::Statement& statement, std::string_view operation) -> std::size_t
{
    ensureStatementWithinBindLimit(statement.parameters.size(), operation);
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

auto DatabaseCore::executeSql(std::string_view statement, std::string_view operation) -> void
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

auto DatabaseCore::relationEndpointExists(model::ModelView model, const db::binding::PrimaryKey& key) -> bool
{
    const auto primaryKeyColumns = db::binding::getPrimaryKeyColumns(model);

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
            db::StatementParameter{.name = parameterName, .value = db::binding::toQueryValue(key[i]), .nullType = std::nullopt});
    }

    statement.sql = std::format("SELECT COUNT(*) FROM {} WHERE {};", dialect.quoteIdentifier(model->tableName), where);
    ensureStatementWithinBindLimit(statement.parameters.size(), "validate relation endpoint");
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

auto DatabaseCore::tableExists(std::string_view tableName) -> bool
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

auto DatabaseCore::ensureRelationTableEndpointsExist(model::ModelView owner) -> void
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

    for (const auto& relation : owner->relations)
    {
        if (relation.kind != model::RelationKind::ManyToMany or not relation.junction.isConfigured() or
            not relation.junction.owningSide)
        {
            continue;
        }

        const auto target = owner.resolveTarget(relation);
        if (target == nullptr)
        {
            throw std::invalid_argument{"ManyToMany relation target is outside the selected schema"};
        }
        const auto ownerPrimaryKey = db::binding::getPrimaryKeyColumns(owner);
        const auto targetPrimaryKey = db::binding::getPrimaryKeyColumns(*target);

        if (ownerPrimaryKey.size() > 1 or targetPrimaryKey.size() > 1)
        {
            requireCapability(capabilities.relations.compositeEndpointKeys, "create relation tables",
                              "composite relation endpoint keys are not supported");
        }

        for (const auto* column : targetPrimaryKey)
        {
            if (not capabilities.supportsColumnType(column->type.value()))
            {
                throw DatabaseError{DatabaseErrorCode::UnsupportedFeature, backendType, "create relation tables",
                                    "A target primary-key type is not supported by the backend"};
            }
        }

        if (not tableExists(owner->tableName) or not tableExists(target->tableName))
        {
            throw std::invalid_argument{"ManyToMany endpoint tables must exist before creating relation tables"};
        }
    }
}

auto DatabaseCore::getBackend() const -> const db::BackendProvider&
{
    if (backend == nullptr or not sql.is_connected())
    {
        throw DatabaseError{DatabaseErrorCode::NotConnected, db::BackendType::Empty, "database operation",
                            "No database session is connected"};
    }

    return *backend;
}

auto DatabaseCore::getCommandGenerator() const -> const db::CommandGenerator&
{
    return getBackend().commandGenerator();
}

auto DatabaseCore::getBackendRuntimeLimits() -> db::BackendRuntimeLimits
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

auto DatabaseCore::ensureModelSupported(model::ModelView descriptor, std::string_view operation) const -> void
{
    const auto& capabilities = getBackendCapabilities();
    const auto& dialect = getBackend().dialect();

    try
    {
        validateModelIdentifiers(dialect, descriptor);
    }
    catch (const std::invalid_argument&)
    {
        throw DatabaseError{DatabaseErrorCode::UnsupportedFeature, backendType, std::string{operation},
                            "A model identifier is not supported by the selected backend"};
    }

    if (operation == "create table")
    {
        if (descriptor->primaryKeyIndices.size() > 1)
        {
            requireCapability(capabilities.schema.compositePrimaryKeys, operation,
                              "composite primary keys are not supported");
        }

        if (std::ranges::any_of(descriptor->columns,
                                [](const auto& column) { return column.kind == model::FieldKind::ToOne; }))
        {
            requireCapability(capabilities.schema.foreignKeys, operation, "foreign keys are not supported");
        }
    }

    for (const auto& column : descriptor->columns)
    {
        if (column.isAutoIncrement and operation == "create table")
        {
            requireCapability(capabilities.schema.autoIncrementPrimaryKey, operation,
                              "auto-increment primary keys are not supported");
        }

        if (column.kind == model::FieldKind::ToOne)
        {
            requireCapability(capabilities.relations.toOne, operation, "to-one relations are not supported");
            const auto relatedModel = descriptor.resolveTarget(column);
            if (relatedModel == nullptr)
            {
                throw DatabaseError{DatabaseErrorCode::UnsupportedFeature, backendType, std::string{operation},
                                    "A related model is outside the selected schema"};
            }

            for (const auto primaryKeyIndex : relatedModel->primaryKeyIndices)
            {
                const auto& relatedColumn = relatedModel->columns[primaryKeyIndex];
                if (not capabilities.supportsColumnType(relatedColumn.type.value()))
                {
                    throw DatabaseError{DatabaseErrorCode::UnsupportedFeature, backendType, std::string{operation},
                                        "A related primary-key column type is not supported by the backend"};
                }
            }

            continue;
        }

        if (not capabilities.supportsColumnType(column.type.value()))
        {
            throw DatabaseError{DatabaseErrorCode::UnsupportedFeature, backendType, std::string{operation},
                                "A model column type is not supported by the backend"};
        }
    }
}

auto DatabaseCore::ensureQuerySupported(model::ModelView descriptor, const query::SelectSpec& spec) const -> void
{
    ensureModelSupported(descriptor, "select");
    const auto& capabilities = getBackendCapabilities();
    const auto& dialect = getBackend().dialect();

    try
    {
        for (const auto& projection : spec.projections)
        {
            (void)dialect.quoteIdentifier(projection.resultField);
        }
    }
    catch (const std::invalid_argument&)
    {
        throw DatabaseError{DatabaseErrorCode::UnsupportedFeature, backendType, "select projection",
                            "A projection alias is not supported by the selected backend"};
    }

    if (spec.projections.empty() and (not spec.groupBy.empty() or spec.having.has_value()))
    {
        requireCapability(capabilities.query.fullModelGrouping, "select",
                          "GROUP BY and HAVING for full-model queries are not supported");
    }

    if (spec.limit.has_value())
    {
        requireCapability(capabilities.query.limit, "select", "LIMIT is not supported");
    }

    if (spec.offset.has_value())
    {
        requireCapability(capabilities.query.offset, "select", "OFFSET is not supported");

        if (not spec.limit.has_value())
        {
            requireCapability(capabilities.query.offsetWithoutLimit, "select", "OFFSET without LIMIT is not supported");
        }

        if (capabilities.query.offsetRequiresOrderBy and spec.orderBy.empty())
        {
            throw DatabaseError{DatabaseErrorCode::UnsupportedFeature, backendType, "select",
                                "This backend requires ORDER BY when OFFSET is used"};
        }
    }

    if (not spec.projections.empty())
    {
        requireCapability(capabilities.query.projections, "select projection", "projections are not supported");
    }

    if (spec.isDistinct and not spec.projections.empty() and capabilities.query.distinctOrderByRequiresProjectedColumn)
    {
        for (const auto& ordering : spec.orderBy)
        {
            const auto orderingSql =
                ordering.isRaw ? std::string{} : renderColumnForValidation(ordering.column, descriptor, spec, dialect);
            const auto ordersByProjectedColumn =
                not ordering.isRaw and
                std::ranges::any_of(spec.projections,
                                    [&orderingSql, descriptor, &spec, &dialect](const auto& projection)
                                    {
                                        const auto* projectedColumn = std::get_if<query::Column>(&projection.source);
                                        return projectedColumn != nullptr and
                                               renderColumnForValidation(*projectedColumn, descriptor, spec, dialect) ==
                                                   orderingSql;
                                    });

            requireCapability(ordersByProjectedColumn, "select projection",
                              "DISTINCT projection queries may order only by projected columns");
        }
    }

    if (not spec.groupBy.empty())
    {
        requireCapability(capabilities.query.groupBy, "select", "GROUP BY is not supported");
    }

    if (spec.having.has_value())
    {
        requireCapability(capabilities.query.having, "select", "HAVING is not supported");
    }

    const auto isAggregateProjection =
        std::ranges::any_of(spec.projections, [](const auto& projection)
                            { return std::holds_alternative<query::AggregateExpression>(projection.source); });
    const auto isAggregateQuery = not spec.groupBy.empty() or spec.having.has_value() or isAggregateProjection;

    if (capabilities.query.strictProjectionGrouping and not spec.projections.empty() and isAggregateQuery)
    {
        std::vector<std::string> groupedColumns;
        groupedColumns.reserve(spec.groupBy.size());

        for (const auto& groupedColumn : spec.groupBy)
        {
            groupedColumns.push_back(renderColumnForValidation(groupedColumn, descriptor, spec, dialect));
        }

        auto isGroupedColumn = [&groupedColumns, descriptor, &spec, &dialect](const query::Column& column)
        {
            const auto rendered = renderColumnForValidation(column, descriptor, spec, dialect);
            return std::ranges::find(groupedColumns, rendered) != groupedColumns.end();
        };

        for (const auto& projection : spec.projections)
        {
            if (const auto* column = std::get_if<query::Column>(&projection.source); column != nullptr)
            {
                requireCapability(isGroupedColumn(*column), "select projection",
                                  "Non-aggregate projected columns must appear in GROUP BY");
            }
        }

        for (const auto& ordering : spec.orderBy)
        {
            if (not ordering.isRaw)
            {
                requireCapability(isGroupedColumn(ordering.column), "select projection",
                                  "Typed ORDER BY columns in aggregate queries must appear in GROUP BY");
            }
        }
    }

    if (spec.predicate.has_value() and containsCollectionPredicate(spec.predicate->getNode()))
    {
        requireCapability(capabilities.query.collectionPredicates, "select", "collection predicates are not supported");
        requireCapability(capabilities.relations.collectionPredicates, "select",
                          "collection predicates are not supported");
    }

    if (not spec.includes.empty())
    {
        requireCapability(capabilities.relations.collectionIncludes, "include collection",
                          "collection includes are not supported");

        for (const auto& include : spec.includes)
        {
            const auto* relation = descriptor.findRelation(include);

            if (relation == nullptr)
            {
                continue;
            }

            const auto target = descriptor.resolveTarget(*relation);
            if (target == nullptr)
            {
                throw DatabaseError{DatabaseErrorCode::UnsupportedFeature, backendType, "include collection",
                                    "An included relation target is outside the selected schema"};
            }
            ensureModelSupported(*target, "include collection");

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

auto DatabaseCore::ensureAffectedRowsAvailable(std::string_view operation) const -> void
{
    if (getBackendCapabilities().mutations.affectedRows != db::AffectedRowsSupport::Reliable)
    {
        throw DatabaseError{DatabaseErrorCode::AffectedRowsUnavailable, backendType, std::string{operation},
                            "The backend cannot report exact affected-row counts"};
    }
}

auto DatabaseCore::requireCapability(bool supported, std::string_view operation, std::string_view message) const -> void
{
    if (not supported)
    {
        throw DatabaseError{DatabaseErrorCode::UnsupportedFeature, backendType, std::string{operation},
                            std::string{message}};
    }
}

auto DatabaseCore::throwTranslatedError(const soci::soci_error& error, DatabaseErrorCode fallback,
                                        std::string_view operation) -> void
{
    if (backend == nullptr)
    {
        throw DatabaseError{DatabaseErrorCode::NotConnected, db::BackendType::Empty, "database operation",
                            "No database backend is available to translate the driver error"};
    }

    if (transaction != nullptr and backend->runtime().statementErrorInvalidatesTransaction(error))
    {
        transactionFailed = true;
    }

    throw backend->runtime().translateError(error, fallback, operation);
}

auto DatabaseCore::ensureStatementWithinBindLimit(std::size_t parameterCount, std::string_view operation) -> void
{
    if (parameterCount == 0)
    {
        return;
    }

    const auto maximum = getBackendRuntimeLimits().maxBindParameters;

    if (maximum.has_value() and parameterCount > maximum.value())
    {
        throw DatabaseError{DatabaseErrorCode::UnsupportedFeature, backendType, std::string{operation},
                            "The statement exceeds the backend bind-parameter limit"};
    }
}
} // namespace orm
