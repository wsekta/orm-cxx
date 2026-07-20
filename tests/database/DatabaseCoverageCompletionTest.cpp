#include <algorithm>
#include <cstdint>
#include <functional>
#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "orm-cxx/database.hpp"
#include "orm-cxx/database/sqlite/SqliteBackend.hpp"
#include "orm-cxx/query.hpp"
#include "tests/CollectionModelsDefinitions.hpp"
#include "tests/ModelsDefinitions.hpp"

namespace database_coverage_completion_models
{
struct AutoOnly
{
    inline static constexpr std::string_view table_name = "coverage_auto_only";
    inline static const std::vector<std::string> auto_increment_columns = {"id"};

    int id;
};

struct NarrowModel
{
    inline static constexpr std::string_view table_name = "coverage_narrow";

    std::optional<std::uint8_t> value;
};

struct ScalarProjection
{
    int value;
};

struct StringTarget
{
    inline static constexpr std::string_view table_name = "coverage_string_targets";

    std::string id;
};

struct IntOwner
{
    inline static constexpr std::string_view table_name = "coverage_int_owners";

    int id;
    orm::ManyToMany<StringTarget> targets;

    inline static const auto relations = orm::relations(orm::manyToMany("targets")
                                                            .through("coverage_int_owner_targets")
                                                            .ownerColumns({"owner_id"})
                                                            .targetColumns({"target_id"}));
};

struct WideTarget
{
    inline static constexpr std::string_view table_name = "coverage_wide_targets";

    unsigned long long id;
};

struct RelatedToWideTarget
{
    inline static constexpr std::string_view table_name = "coverage_related_to_wide_target";

    std::string id;
    WideTarget target;
};
} // namespace database_coverage_completion_models

namespace
{
using namespace database_coverage_completion_models;
using namespace orm::query;

class StaticCreateTableCommand final : public orm::db::commands::CreateTableCommand
{
public:
    explicit StaticCreateTableCommand(std::string sqlInit) : sql{std::move(sqlInit)} {}

    auto createTable(const orm::model::ModelInfo& /*modelInfo*/) const -> std::string override
    {
        return sql;
    }

private:
    std::string sql;
};

class StaticDropTableCommand final : public orm::db::commands::DropTableCommand
{
public:
    explicit StaticDropTableCommand(std::string sqlInit) : sql{std::move(sqlInit)} {}

    auto dropTable(const orm::model::ModelInfo& /*modelInfo*/) const -> std::string override
    {
        return sql;
    }

private:
    std::string sql;
};

class StaticInsertCommand final : public orm::db::commands::InsertCommand
{
public:
    explicit StaticInsertCommand(std::string sqlInit) : sql{std::move(sqlInit)} {}

    auto insert(const orm::model::ModelInfo& /*modelInfo*/) const -> std::string override
    {
        return sql;
    }

private:
    std::string sql;
};

class StaticSelectCommand final : public orm::db::commands::SelectCommand
{
public:
    explicit StaticSelectCommand(orm::db::Statement statementInit) : statement{std::move(statementInit)} {}

    auto select(const orm::query::QueryData& /*queryData*/) const -> orm::db::SelectStatement override
    {
        return statement;
    }

private:
    orm::db::Statement statement;
};

class StaticUpdateCommand final : public orm::db::commands::UpdateCommand
{
public:
    explicit StaticUpdateCommand(orm::db::Statement statementInit) : statement{std::move(statementInit)} {}

    auto update(const orm::query::UpdateData& /*updateData*/) const -> orm::db::Statement override
    {
        return statement;
    }

private:
    orm::db::Statement statement;
};

class StaticDeleteCommand final : public orm::db::commands::DeleteCommand
{
public:
    explicit StaticDeleteCommand(orm::db::Statement statementInit) : statement{std::move(statementInit)} {}

    auto remove(const orm::model::ModelInfo& /*modelInfo*/, const orm::query::Predicate& /*predicate*/) const
        -> orm::db::Statement override
    {
        return statement;
    }

private:
    orm::db::Statement statement;
};

struct CommandScripts
{
    std::string create = "SELECT 1;";
    std::string drop = "SELECT 1;";
    std::string insert = "SELECT 1;";
    orm::db::Statement select{.sql = "SELECT 1;"};
    orm::db::Statement update{.sql = "SELECT 1;"};
    orm::db::Statement remove{.sql = "SELECT 1;"};
};

auto makeScriptedGenerator(CommandScripts scripts) -> std::unique_ptr<orm::db::CommandGenerator>
{
    return std::make_unique<orm::db::CommandGenerator>(
        std::make_unique<StaticCreateTableCommand>(std::move(scripts.create)),
        std::make_unique<StaticDropTableCommand>(std::move(scripts.drop)),
        std::make_unique<StaticInsertCommand>(std::move(scripts.insert)),
        std::make_unique<StaticSelectCommand>(std::move(scripts.select)),
        std::make_unique<StaticUpdateCommand>(std::move(scripts.update)),
        std::make_unique<StaticDeleteCommand>(std::move(scripts.remove)));
}

enum class OnConnectAction
{
    Delegate,
    CloseThenThrowDriverError,
    ThrowUnknown,
    BeginTransaction,
};

enum class NormalizeAction
{
    Delegate,
    ThrowDatabaseError,
    ThrowStandardError,
};

class ConfigurableRuntime final : public orm::db::BackendRuntime
{
public:
    explicit ConfigurableRuntime(const orm::db::BackendRuntime& delegateInit) : delegate{delegateInit} {}

    auto onConnect(soci::session& session) const -> void override
    {
        delegate.onConnect(session);

        switch (onConnectAction)
        {
        case OnConnectAction::Delegate:
            return;
        case OnConnectAction::CloseThenThrowDriverError:
            session.close();
            throw soci::soci_error{"coverage connect driver failure"};
        case OnConnectAction::ThrowUnknown:
            throw 7;
        case OnConnectAction::BeginTransaction:
            session.begin();
            return;
        }
    }

    auto tableExists(soci::session& session, std::string_view tableName) const -> bool override
    {
        if (throwFromTableExists)
        {
            if (beforeTableExistsFailure)
            {
                beforeTableExistsFailure();
            }

            throw soci::soci_error{"coverage table inspection failure"};
        }

        return delegate.tableExists(session, tableName);
    }

    auto limits(soci::session& session) const -> orm::db::BackendRuntimeLimits override
    {
        if (throwFromLimits)
        {
            throw soci::soci_error{"coverage runtime limits failure"};
        }

        if (overrideLimits)
        {
            return limitsValue;
        }

        return delegate.limits(session);
    }

    auto normalizeAffectedRows(long long affectedRows) const -> std::size_t override
    {
        switch (normalizeAction)
        {
        case NormalizeAction::Delegate:
            return delegate.normalizeAffectedRows(affectedRows);
        case NormalizeAction::ThrowDatabaseError:
            throw orm::DatabaseError{orm::DatabaseErrorCode::Constraint, orm::db::BackendType::Postgres,
                                     "coverage mutation", "coverage database error"};
        case NormalizeAction::ThrowStandardError:
            throw std::runtime_error{"coverage affected rows failure"};
        }

        throw std::logic_error{"unreachable normalization action"};
    }

    auto bind(soci::values& values, std::string_view name, const orm::db::BoundValue& value) const -> void override
    {
        if (throwFromBind)
        {
            throw orm::db::binding::ConversionError{"coverage binding failure"};
        }

        delegate.bind(values, name, value);
    }

    auto translateError(const soci::soci_error& /*error*/, orm::DatabaseErrorCode fallback,
                        std::string_view operation) const -> orm::DatabaseError override
    {
        return orm::DatabaseError{fallback, orm::db::BackendType::Postgres, std::string{operation},
                                  "coverage translated driver error"};
    }

    OnConnectAction onConnectAction = OnConnectAction::Delegate;
    NormalizeAction normalizeAction = NormalizeAction::Delegate;
    bool throwFromBind = false;
    bool throwFromTableExists = false;
    bool throwFromLimits = false;
    bool overrideLimits = false;
    std::function<void()> beforeTableExistsFailure;
    orm::db::BackendRuntimeLimits limitsValue;

private:
    const orm::db::BackendRuntime& delegate;
};

class ConfigurableBackend final : public orm::db::BackendProvider
{
private:
    orm::db::sqlite::SqliteBackend sqlite;

public:
    explicit ConfigurableBackend(std::unique_ptr<orm::db::CommandGenerator> customGeneratorInit = nullptr)
        : runtimeStrategy{sqlite.runtime()},
          capabilitiesValue{sqlite.capabilities()},
          customGenerator{std::move(customGeneratorInit)}
    {
    }

    auto type() const noexcept -> orm::db::BackendType override
    {
        return orm::db::BackendType::Postgres;
    }

    auto acceptsConnectionString(std::string_view connectionString) const noexcept -> bool override
    {
        return connectionString.starts_with("sqlite3://");
    }

    auto capabilities() const noexcept -> const orm::db::BackendCapabilities& override
    {
        return capabilitiesValue;
    }

    auto dialect() const noexcept -> const orm::db::SqlDialect& override
    {
        return sqlite.dialect();
    }

    auto runtime() const noexcept -> const orm::db::BackendRuntime& override
    {
        return runtimeStrategy;
    }

    auto commandGenerator() const noexcept -> const orm::db::CommandGenerator& override
    {
        return customGenerator != nullptr ? *customGenerator : sqlite.commandGenerator();
    }

    ConfigurableRuntime runtimeStrategy;
    orm::db::BackendCapabilities capabilitiesValue;

private:
    std::unique_ptr<orm::db::CommandGenerator> customGenerator;
};

class ModelInfoRestore
{
public:
    explicit ModelInfoRestore(orm::model::ModelInfo& targetInit) : target{targetInit}, original{targetInit} {}

    ~ModelInfoRestore()
    {
        target = std::move(original);
    }

    ModelInfoRestore(const ModelInfoRestore&) = delete;
    auto operator=(const ModelInfoRestore&) -> ModelInfoRestore& = delete;

private:
    orm::model::ModelInfo& target;
    orm::model::ModelInfo original;
};

class DatabaseBundle
{
public:
    explicit DatabaseBundle(std::unique_ptr<orm::db::CommandGenerator> generator = nullptr)
    {
        auto factory = orm::db::CommandGeneratorFactory{};
        auto provider = std::make_unique<ConfigurableBackend>(std::move(generator));
        backend = provider.get();
        factory.registerBackend(std::move(provider));
        database = std::make_unique<orm::Database>(std::move(factory));
    }

    auto connect() -> void
    {
        database->connect(orm::db::BackendType::Postgres, "sqlite3://:memory:");
    }

    ConfigurableBackend* backend{};
    std::unique_ptr<orm::Database> database;
};

template <typename Operation>
auto expectDatabaseError(Operation&& operation, orm::DatabaseErrorCode code, std::string_view expectedOperation = {})
    -> void
{
    try
    {
        std::forward<Operation>(operation)();
        FAIL() << "Expected orm::DatabaseError";
    }
    catch (const orm::DatabaseError& error)
    {
        EXPECT_EQ(error.getCode(), code);
        if (not expectedOperation.empty())
        {
            EXPECT_EQ(error.getOperation(), expectedOperation);
        }
    }
}

auto column(std::string name, orm::model::ColumnType type) -> orm::model::ColumnInfo
{
    return orm::model::ColumnInfo{.fieldName = name,
                                  .name = std::move(name),
                                  .type = type,
                                  .isPrimaryKey = false,
                                  .isForeignModel = false,
                                  .isAutoIncrement = false,
                                  .isUnique = false,
                                  .isNotNull = true};
}

auto removeSupportedType(orm::db::BackendCapabilities& capabilities, orm::model::ColumnType type) -> void
{
    std::erase(capabilities.supportedColumnTypes, type);
}
} // namespace

TEST(DatabaseCoverageCompletionTest, serializedModelBindingRejectsIncompatibleAndUnsupportedLogicalTypes)
{
    const orm::db::sqlite::SqliteBackend sqlite;
    soci::values serialized;
    soci::values target;
    serialized.set("flag", 2);

    const auto incompatible = orm::model::ModelInfo{.tableName = "coverage_binding",
                                                    .columnsInfo = {column("flag", orm::model::ColumnType::Bool)}};
    EXPECT_THROW(orm::detail::bindModelParameters(sqlite.runtime(), target, serialized, incompatible),
                 orm::db::binding::ConversionError);

    for (const auto unsupported :
         {orm::model::ColumnType::Uuid, orm::model::ColumnType::Unknown, orm::model::ColumnType::OneToOne})
    {
        const auto modelInfo =
            orm::model::ModelInfo{.tableName = "coverage_binding", .columnsInfo = {column("flag", unsupported)}};
        EXPECT_THROW(orm::detail::bindModelParameters(sqlite.runtime(), target, serialized, modelInfo),
                     std::invalid_argument);
    }
}

TEST(DatabaseCoverageCompletionTest, typedConnectRejectsAlreadyConnectedDatabase)
{
    DatabaseBundle bundle;
    bundle.connect();

    expectDatabaseError([&bundle]() { bundle.database->connect(orm::db::BackendType::Postgres, "sqlite3://:memory:"); },
                        orm::DatabaseErrorCode::AlreadyConnected, "connect");
    bundle.database->disconnect();
}

TEST(DatabaseCoverageCompletionTest, driverConnectFailureHandlesSessionAlreadyClosedByRuntime)
{
    DatabaseBundle bundle;
    bundle.backend->runtimeStrategy.onConnectAction = OnConnectAction::CloseThenThrowDriverError;

    expectDatabaseError([&bundle]() { bundle.connect(); }, orm::DatabaseErrorCode::Connection, "connect");
    EXPECT_FALSE(bundle.database->isConnected());
}

TEST(DatabaseCoverageCompletionTest, unknownConnectFailureClosesTheOpenedSession)
{
    DatabaseBundle bundle;
    bundle.backend->runtimeStrategy.onConnectAction = OnConnectAction::ThrowUnknown;

    expectDatabaseError([&bundle]() { bundle.connect(); }, orm::DatabaseErrorCode::Connection, "connect");
    EXPECT_FALSE(bundle.database->isConnected());

    bundle.backend->runtimeStrategy.onConnectAction = OnConnectAction::Delegate;
    EXPECT_NO_THROW(bundle.connect());
    EXPECT_TRUE(bundle.database->isConnected());
    bundle.database->disconnect();
}

TEST(DatabaseCoverageCompletionTest, disconnectWithoutBackendIsIdempotent)
{
    orm::Database database;

    EXPECT_NO_THROW(database.disconnect());
    EXPECT_EQ(database.getBackendType(), orm::db::BackendType::Empty);
}

TEST(DatabaseCoverageCompletionTest, nestedDriverTransactionIsTranslatedByBeginTransaction)
{
    DatabaseBundle bundle;
    bundle.backend->runtimeStrategy.onConnectAction = OnConnectAction::BeginTransaction;
    bundle.connect();

    expectDatabaseError([&bundle]() { bundle.database->beginTransaction(); }, orm::DatabaseErrorCode::Transaction,
                        "begin transaction");
}

TEST(DatabaseCoverageCompletionTest, commitFailureResetsTheTransactionAndTranslatesTheDriverError)
{
    DatabaseBundle bundle{makeScriptedGenerator(CommandScripts{.create = "COMMIT;"})};
    bundle.connect();
    bundle.database->beginTransaction();
    bundle.database->createTable<models::ModelWithOneField>();

    expectDatabaseError([&bundle]() { bundle.database->commitTransaction(); }, orm::DatabaseErrorCode::Transaction,
                        "commit transaction");
    EXPECT_NO_THROW(bundle.database->disconnect());
}

TEST(DatabaseCoverageCompletionTest, rollbackFailureResetsTheTransactionAndTranslatesTheDriverError)
{
    DatabaseBundle bundle{makeScriptedGenerator(CommandScripts{.create = "ROLLBACK;"})};
    bundle.connect();
    bundle.database->beginTransaction();
    bundle.database->createTable<models::ModelWithOneField>();

    expectDatabaseError([&bundle]() { bundle.database->rollbackTransaction(); }, orm::DatabaseErrorCode::Transaction,
                        "rollback transaction");
    EXPECT_NO_THROW(bundle.database->disconnect());
}

TEST(DatabaseCoverageCompletionTest, disconnectReportsRollbackFailureAfterTransactionWasHandledExternally)
{
    DatabaseBundle bundle{makeScriptedGenerator(CommandScripts{.create = "COMMIT;"})};
    bundle.connect();
    bundle.database->beginTransaction();
    bundle.database->createTable<models::ModelWithOneField>();

    expectDatabaseError([&bundle]() { bundle.database->disconnect(); }, orm::DatabaseErrorCode::Transaction,
                        "rollback during disconnect");
    EXPECT_FALSE(bundle.database->isConnected());
}

TEST(DatabaseCoverageCompletionTest, mutationTranslatesConversionAndAffectedRowFailures)
{
    DatabaseBundle bundle;
    bundle.connect();
    bundle.database->createTable<models::SomeDataModel>();
    bundle.database->insert(models::SomeDataModel{1, "one", 1.0});
    orm::Update<models::SomeDataModel> update;
    update.set(col("field2"), "updated").where(col("field1") == 1);

    bundle.backend->runtimeStrategy.throwFromBind = true;
    expectDatabaseError([&bundle, &update]() { (void)bundle.database->update(update); },
                        orm::DatabaseErrorCode::Conversion, "update");

    bundle.backend->runtimeStrategy.throwFromBind = false;
    bundle.backend->runtimeStrategy.normalizeAction = NormalizeAction::ThrowDatabaseError;
    expectDatabaseError([&bundle, &update]() { (void)bundle.database->update(update); },
                        orm::DatabaseErrorCode::Constraint, "coverage mutation");

    bundle.backend->runtimeStrategy.normalizeAction = NormalizeAction::ThrowStandardError;
    expectDatabaseError([&bundle, &update]() { (void)bundle.database->update(update); },
                        orm::DatabaseErrorCode::AffectedRowsUnavailable, "update");
}

TEST(DatabaseCoverageCompletionTest, executeSqlTranslatesDriverErrors)
{
    DatabaseBundle bundle{makeScriptedGenerator(CommandScripts{.create = "THIS IS NOT SQL;"})};
    bundle.connect();

    expectDatabaseError([&bundle]() { bundle.database->createTable<models::ModelWithOneField>(); },
                        orm::DatabaseErrorCode::Statement, "create table");
}

TEST(DatabaseCoverageCompletionTest, relationEndpointValidationTranslatesBindingAndDriverErrors)
{
    DatabaseBundle bundle;
    bundle.connect();
    bundle.database->createTable<collection_models::Author>();
    bundle.database->createTable<collection_models::Book>();
    const auto author = collection_models::Author{1, "author", {}};
    const auto book = collection_models::Book{10, "book", std::nullopt};
    bundle.database->insert(author);
    bundle.database->insert(book);

    bundle.backend->runtimeStrategy.throwFromBind = true;
    expectDatabaseError([&bundle, &author, &book]() { (void)bundle.database->link(author, "books", book); },
                        orm::DatabaseErrorCode::Conversion, "validate relation endpoint");

    bundle.backend->runtimeStrategy.throwFromBind = false;
    bundle.database->deleteTable<collection_models::Book>();
    expectDatabaseError([&bundle, &author, &book]() { (void)bundle.database->link(author, "books", book); },
                        orm::DatabaseErrorCode::Statement, "validate relation endpoint");
}

TEST(DatabaseCoverageCompletionTest, relationTableEndpointInspectionTranslatesDriverErrors)
{
    DatabaseBundle bundle;
    bundle.backend->runtimeStrategy.throwFromTableExists = true;
    bundle.connect();

    expectDatabaseError([&bundle]() { bundle.database->createRelationTables<collection_models::User>(); },
                        orm::DatabaseErrorCode::Statement, "inspect schema");
}

TEST(DatabaseCoverageCompletionTest, driverErrorAfterReentrantDisconnectUsesNotConnectedFallback)
{
    DatabaseBundle bundle;
    bundle.backend->runtimeStrategy.throwFromTableExists = true;
    bundle.backend->runtimeStrategy.beforeTableExistsFailure = [&bundle]() { bundle.database->disconnect(); };
    bundle.connect();

    expectDatabaseError([&bundle]() { bundle.database->createRelationTables<collection_models::User>(); },
                        orm::DatabaseErrorCode::NotConnected, "database operation");
}

TEST(DatabaseCoverageCompletionTest, relationEndpointCapabilityChecksDistinguishOwnerAndTargetTypes)
{
    {
        DatabaseBundle bundle;
        removeSupportedType(bundle.backend->capabilitiesValue, orm::model::ColumnType::Int);
        bundle.connect();
        expectDatabaseError([&bundle]() { bundle.database->createRelationTables<IntOwner>(); },
                            orm::DatabaseErrorCode::UnsupportedFeature, "create relation tables");
    }

    {
        DatabaseBundle bundle;
        removeSupportedType(bundle.backend->capabilitiesValue, orm::model::ColumnType::String);
        bundle.connect();
        expectDatabaseError([&bundle]() { bundle.database->createRelationTables<IntOwner>(); },
                            orm::DatabaseErrorCode::UnsupportedFeature, "create relation tables");
    }
}

TEST(DatabaseCoverageCompletionTest, relationEndpointValidationSkipsInverseMappingsBeforeOwnedMapping)
{
    DatabaseBundle bundle;
    bundle.connect();
    bundle.database->createTable<collection_models::User>();
    bundle.database->createTable<collection_models::Role>();

    auto& userInfo = orm::Model<collection_models::User>::getModelInfo();
    const ModelInfoRestore restore{userInfo};
    const auto& roleInfo = orm::Model<collection_models::Role>::getModelInfo();
    userInfo.relationsInfo.insert(userInfo.relationsInfo.begin(), roleInfo.relationsInfo.front());

    EXPECT_NO_THROW(bundle.database->createRelationTables<collection_models::User>());
}

TEST(DatabaseCoverageCompletionTest, includeTranslatesRuntimeLimitErrorsAndRejectsAnInsufficientBudget)
{
    DatabaseBundle bundle;
    bundle.connect();
    bundle.database->createTable<collection_models::Author>();
    bundle.database->createTable<collection_models::Book>();
    bundle.database->insert(collection_models::Author{1, "author", {}});
    orm::Query<collection_models::Author> query;
    query.include("books");

    bundle.backend->runtimeStrategy.throwFromLimits = true;
    expectDatabaseError([&bundle, &query]() { (void)bundle.database->select(query); },
                        orm::DatabaseErrorCode::Statement, "read backend limits");

    bundle.backend->runtimeStrategy.throwFromLimits = false;
    bundle.backend->runtimeStrategy.overrideLimits = true;
    bundle.backend->runtimeStrategy.limitsValue.maxBindParameters = 0;
    expectDatabaseError([&bundle, &query]() { (void)bundle.database->select(query); },
                        orm::DatabaseErrorCode::UnsupportedFeature, "include collection");
}

TEST(DatabaseCoverageCompletionTest, modelCapabilityChecksDistinguishRelatedAndDirectColumnTypes)
{
    {
        DatabaseBundle bundle;
        removeSupportedType(bundle.backend->capabilitiesValue, orm::model::ColumnType::UnsignedLongLong);
        bundle.connect();
        expectDatabaseError([&bundle]() { bundle.database->createTable<RelatedToWideTarget>(); },
                            orm::DatabaseErrorCode::UnsupportedFeature, "create table");
    }

    {
        DatabaseBundle bundle;
        removeSupportedType(bundle.backend->capabilitiesValue, orm::model::ColumnType::Int);
        bundle.connect();
        expectDatabaseError([&bundle]() { bundle.database->createTable<models::ModelWithOneField>(); },
                            orm::DatabaseErrorCode::UnsupportedFeature, "create table");
    }
}

TEST(DatabaseCoverageCompletionTest, queryCapabilityChecksHandleNegatedCollectionsAndOrderedOffsetRequirement)
{
    {
        DatabaseBundle bundle;
        bundle.backend->capabilitiesValue.query.collectionPredicates = false;
        bundle.connect();
        orm::Query<collection_models::Author> query;
        query.where(!any("books", col("title") == "book"));

        expectDatabaseError([&bundle, &query]() { (void)bundle.database->select(query); },
                            orm::DatabaseErrorCode::UnsupportedFeature, "select");
    }

    {
        DatabaseBundle bundle;
        bundle.backend->capabilitiesValue.query.offsetRequiresOrderBy = true;
        bundle.connect();
        orm::Query<models::ModelWithOneField> query;
        query.offset(1);

        expectDatabaseError([&bundle, &query]() { (void)bundle.database->select(query); },
                            orm::DatabaseErrorCode::UnsupportedFeature, "select");
    }
}

TEST(DatabaseCoverageCompletionTest, mutationRejectsBackendWithoutReliableAffectedRows)
{
    DatabaseBundle bundle;
    bundle.backend->capabilitiesValue.mutations.affectedRows = orm::db::AffectedRowsSupport::Unavailable;
    bundle.connect();
    orm::Update<models::ModelWithOneField> update;
    update.set(col("field1"), 2).where(col("field1") == 1);

    expectDatabaseError([&bundle, &update]() { (void)bundle.database->update(update); },
                        orm::DatabaseErrorCode::AffectedRowsUnavailable, "update");
}

TEST(DatabaseCoverageCompletionTest, fullModelSelectTranslatesHydrationConversionErrors)
{
    auto scripts = CommandScripts{};
    scripts.create = "CREATE TABLE \"coverage_narrow\" (\"value\" TINYINT);";
    scripts.insert = "INSERT INTO \"coverage_narrow\" (\"value\") VALUES (300);";
    scripts.select.sql = "SELECT \"value\" AS \"coverage_narrow_value\" FROM \"coverage_narrow\";";
    DatabaseBundle bundle{makeScriptedGenerator(std::move(scripts))};
    bundle.connect();
    bundle.database->createTable<NarrowModel>();
    bundle.database->insert(AutoOnly{});
    orm::Query<NarrowModel> query;

    expectDatabaseError([&bundle, &query]() { (void)bundle.database->select(query); },
                        orm::DatabaseErrorCode::Conversion, "select");
}

TEST(DatabaseCoverageCompletionTest, projectionSelectTranslatesDriverErrors)
{
    DatabaseBundle bundle;
    bundle.connect();
    orm::ProjectionQuery<models::ModelWithOneField, ScalarProjection> query;
    query.project(as("value", col("field1")));

    expectDatabaseError([&bundle, &query]() { (void)bundle.database->select(query); },
                        orm::DatabaseErrorCode::Statement, "select projection");
}

TEST(DatabaseCoverageCompletionTest, insertExecutesUnboundCommandForAutoOnlyModel)
{
    auto scripts = CommandScripts{};
    scripts.create = "CREATE TABLE \"coverage_auto_only\" (\"id\" INTEGER PRIMARY KEY AUTOINCREMENT);";
    scripts.insert = "INSERT INTO \"coverage_auto_only\" DEFAULT VALUES;";
    DatabaseBundle bundle{makeScriptedGenerator(std::move(scripts))};
    bundle.connect();

    bundle.database->createTable<AutoOnly>();
    EXPECT_NO_THROW(bundle.database->insert(AutoOnly{}));
}
