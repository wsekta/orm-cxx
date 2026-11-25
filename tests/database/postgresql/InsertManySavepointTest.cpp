module;

#include "tests/UnitTestPrelude.hpp"

module orm;

import :internal;
import :test_support;
import :foundation;
import :model;
import :expressions;
import :dynamic_query;
import :static_plan;
import :sql;
import :database;

using namespace orm::test::fixtures;

using InsertManySavepointSchema = orm::Schema<models::ModelWithId>;

namespace
{
auto makeRows(int count, int firstId = 1) -> std::vector<models::ModelWithId>
{
    std::vector<models::ModelWithId> rows;
    rows.reserve(static_cast<std::size_t>(count));

    for (int index = 0; index < count; ++index)
    {
        rows.push_back(models::ModelWithId{firstId + index, index, std::format("row-{}", index)});
    }

    return rows;
}

auto selectAll(orm::OrmContext<InsertManySavepointSchema>& database) -> std::vector<models::ModelWithId>
{
    orm::Query<models::ModelWithId> query;
    query.orderBy(orm::query::asc(orm::query::col<&models::ModelWithId::id>()));
    return database.select(query);
}
} // namespace

class InsertManySavepointTest : public DatabaseTest<InsertManySavepointSchema>
{
};

TEST_P(InsertManySavepointTest, callerTransactionKeepsItsEarlierRowAndCommitsTheBatch)
{
    createTable<models::ModelWithId>();
    connection.beginTransaction();
    database.insert(models::ModelWithId{1, 0, "caller"});

    const auto batch = makeRows(70, 2);
    ASSERT_NO_THROW(database.insertMany(std::span<const models::ModelWithId>{batch}));

    const auto insideTransaction = selectAll(database);
    ASSERT_EQ(insideTransaction.size(), 71);
    EXPECT_EQ(insideTransaction.front().id, 1);
    EXPECT_EQ(insideTransaction.front().field2, "caller");
    EXPECT_EQ(insideTransaction.back().id, 71);

    connection.commitTransaction();

    const auto afterCommit = selectAll(database);
    ASSERT_EQ(afterCommit.size(), 71);
    EXPECT_EQ(afterCommit.front().field2, "caller");
    EXPECT_EQ(afterCommit.back().id, 71);
}

TEST_P(InsertManySavepointTest, callerRollbackAfterSuccessfulInsertManyRemovesTheBatch)
{
    createTable<models::ModelWithId>();
    connection.beginTransaction();
    database.insert(models::ModelWithId{1, 0, "caller"});

    const auto batch = makeRows(40, 2);
    ASSERT_NO_THROW(database.insertMany(std::span<const models::ModelWithId>{batch}));
    ASSERT_EQ(selectAll(database).size(), 41);

    connection.rollbackTransaction();

    EXPECT_TRUE(selectAll(database).empty());
}

TEST_P(InsertManySavepointTest, duplicateInSecondChunkRecoversTheCallerTransaction)
{
    createTable<models::ModelWithId>();
    connection.beginTransaction();
    database.insert(models::ModelWithId{1, 0, "baseline"});

    auto rows = makeRows(70, 2);
    rows[40].id = 1;

    try
    {
        database.insertMany(std::span<const models::ModelWithId>{rows});
        FAIL() << "Expected orm::DatabaseError";
    }
    catch (const orm::DatabaseError& error)
    {
        EXPECT_EQ(error.getCode(), orm::DatabaseErrorCode::Constraint);
        EXPECT_EQ(error.getOperation(), "insert many");
        ASSERT_TRUE(error.getNativeCode().has_value());
        EXPECT_EQ(error.getNativeCode().value(), "23505");
    }

    const auto afterFailure = selectAll(database);
    ASSERT_EQ(afterFailure.size(), 1);
    EXPECT_EQ(afterFailure[0].id, 1);
    EXPECT_EQ(afterFailure[0].field2, "baseline");

    EXPECT_NO_THROW(database.insert(models::ModelWithId{1001, 5, "after-recovery"}));
    EXPECT_NO_THROW(connection.commitTransaction());

    const auto afterCommit = selectAll(database);
    ASSERT_EQ(afterCommit.size(), 2);
    EXPECT_EQ(afterCommit[0].id, 1);
    EXPECT_EQ(afterCommit[1].id, 1001);
}

TEST_P(InsertManySavepointTest, secondInsertManyAfterRecoveredFailureSucceeds)
{
    createTable<models::ModelWithId>();
    connection.beginTransaction();

    auto failing = makeRows(70, 2);
    failing[40].id = failing[0].id;

    EXPECT_THROW(database.insertMany(std::span<const models::ModelWithId>{failing}), orm::DatabaseError);

    const auto recovered = makeRows(3, 100);
    ASSERT_NO_THROW(database.insertMany(std::span<const models::ModelWithId>{recovered}));
    ASSERT_NO_THROW(connection.commitTransaction());

    const auto stored = selectAll(database);
    ASSERT_EQ(stored.size(), 3);
    EXPECT_EQ(stored[0].id, 100);
    EXPECT_EQ(stored[2].id, 102);
}

TEST_P(InsertManySavepointTest, failedBatchRecoversScopedTransactionAndLeavesCommitToGuard)
{
    createTable<models::ModelWithId>();
    auto transaction = connection.scopedTransaction();
    database.insert(models::ModelWithId{1, 0, "before batch"});

    auto failing = makeRows(70, 2);
    failing[40].id = 1;
    try
    {
        database.insertMany(std::span<const models::ModelWithId>{failing});
        FAIL() << "Expected orm::DatabaseError";
    }
    catch (const orm::DatabaseError& error)
    {
        EXPECT_EQ(error.getCode(), orm::DatabaseErrorCode::Constraint);
        EXPECT_EQ(error.getOperation(), "insert many");
        EXPECT_EQ(error.getNativeCode(), std::optional<std::string>{"23505"});
    }
    const auto afterFailure = selectAll(database);
    ASSERT_EQ(afterFailure.size(), 1);
    EXPECT_EQ(afterFailure.front().field2, "before batch");

    const auto recovered = makeRows(3, 100);
    ASSERT_NO_THROW(database.insertMany(std::span<const models::ModelWithId>{recovered}));
    ASSERT_NO_THROW(transaction.commit());
    const auto stored = selectAll(database);
    ASSERT_EQ(stored.size(), 4);
    EXPECT_EQ(stored.back().id, 102);
}

TEST_P(InsertManySavepointTest, consecutiveBatchesInsideOneCallerTransactionAllCommit)
{
    createTable<models::ModelWithId>();
    connection.beginTransaction();

    const auto first = makeRows(35, 1);
    const auto second = makeRows(35, 36);
    ASSERT_NO_THROW(database.insertMany(std::span<const models::ModelWithId>{first}));
    ASSERT_NO_THROW(database.insertMany(std::span<const models::ModelWithId>{second}));

    ASSERT_NO_THROW(connection.commitTransaction());

    EXPECT_EQ(selectAll(database).size(), 70);
}

TEST_P(InsertManySavepointTest, preFailedCallerTransactionIsRejectedWithoutBeingReset)
{
    createTable<models::ModelWithId>();
    connection.beginTransaction();
    database.insert(models::ModelWithId{1, 0, "first"});

    EXPECT_THROW(database.insert(models::ModelWithId{1, 0, "duplicate"}), orm::DatabaseError);

    const auto rows = makeRows(3, 2);

    try
    {
        database.insertMany(std::span<const models::ModelWithId>{rows});
        FAIL() << "Expected orm::DatabaseError";
    }
    catch (const orm::DatabaseError& error)
    {
        EXPECT_EQ(error.getCode(), orm::DatabaseErrorCode::Transaction);
        EXPECT_EQ(error.getOperation(), "insert many");
    }

    // The rejected call must not clear the failed flag, so commit still refuses.
    EXPECT_THROW(connection.commitTransaction(), orm::DatabaseError);

    EXPECT_NO_THROW(connection.rollbackTransaction());
    EXPECT_TRUE(selectAll(database).empty());
}

INSTANTIATE_TEST_SUITE_P(Postgresql, InsertManySavepointTest, ::testing::Values(postgresqlBackendTestConfig),
                         backendTestName);

namespace
{
/**
 * @brief Models a driver violation that is deliberately not an std::exception.
 *
 * SOCI contracts report driver failures as soci_error, but a custom driver (or command generator) can still
 * break that contract. The insertMany cleanup paths must keep the original failure instead of hiding it.
 */
struct UnknownInsertManyDriverFailure
{
};

struct InsertManyFaultProbe
{
    bool failChunk = false;
    bool failSavepointRecovery = false;
    bool failSavepointRecoveryWithUnknownFailure = false;
    bool failRollback = false;
    bool failRollbackWithUnknownFailure = false;
    bool throwFromInsertMany = false;
};

/**
 * @brief Accepts every bound value; the fake statement only inspects the prepared command.
 */
class InsertManyFaultUse final : public soci::details::standard_use_type_backend
{
public:
    auto bind_by_pos(int& /*position*/, void* /*data*/, soci::details::exchange_type /*type*/,
                     bool /*readOnly*/) -> void override
    {
    }
    auto bind_by_name(std::string const& /*name*/, void* /*data*/, soci::details::exchange_type /*type*/,
                      bool /*readOnly*/) -> void override
    {
    }
    auto pre_use(soci::indicator const*) -> void override {}
    auto post_use(bool /*gotData*/, soci::indicator*) -> void override {}
    auto clean_up() -> void override {}
};

/**
 * @brief Statement double that records the prepared command and fails the requested steps deterministically.
 */
class InsertManyFaultStatement final : public soci::details::statement_backend
{
public:
    explicit InsertManyFaultStatement(InsertManyFaultProbe& probeInit) : probe{probeInit} {}

    auto alloc() -> void override {}
    auto clean_up() -> void override {}
    auto prepare(std::string const& query, soci::details::statement_type /*type*/) -> void override
    {
        prepared = query;

        if (probe.failSavepointRecovery and prepared.starts_with("ROLLBACK TO SAVEPOINT"))
        {
            if (probe.failSavepointRecoveryWithUnknownFailure)
            {
                probe.failSavepointRecoveryWithUnknownFailure = false;
                throw UnknownInsertManyDriverFailure{};
            }
            throw soci::soci_error{"injected savepoint recovery failure"};
        }
    }
    auto execute(int /*number*/) -> exec_fetch_result override
    {
        if (probe.failChunk and prepared.starts_with("INSERT"))
        {
            throw soci::soci_error{"injected batch statement failure"};
        }
        return ef_success;
    }
    auto fetch(int /*number*/) -> exec_fetch_result override
    {
        return ef_no_data;
    }
    auto get_affected_rows() -> long long override
    {
        return 1;
    }
    auto get_number_of_rows() -> int override
    {
        return 0;
    }
    [[nodiscard]] auto get_parameter_name(int /*index*/) const -> std::string override
    {
        return {};
    }
    auto rewrite_for_procedure_call(std::string const& query) -> std::string override
    {
        return query;
    }
    auto prepare_for_describe() -> int override
    {
        return 0;
    }
    auto describe_column(int /*column*/, soci::db_type& /*type*/, std::string& /*name*/) -> void override {}
    auto make_into_type_backend() -> soci::details::standard_into_type_backend* override
    {
        return nullptr;
    }
    auto make_use_type_backend() -> soci::details::standard_use_type_backend* override
    {
        return new InsertManyFaultUse{};
    }
    auto make_vector_into_type_backend() -> soci::details::vector_into_type_backend* override
    {
        return nullptr;
    }
    auto make_vector_use_type_backend() -> soci::details::vector_use_type_backend* override
    {
        return nullptr;
    }

private:
    InsertManyFaultProbe& probe;
    std::string prepared;
};

/**
 * @brief Session double whose transaction control and statement creation can fail on demand.
 */
class InsertManyFaultSession final : public soci::details::session_backend
{
public:
    explicit InsertManyFaultSession(InsertManyFaultProbe& probeInit) : probe{probeInit} {}

    auto is_connected() -> bool override
    {
        return true;
    }
    auto begin() -> void override {}
    auto commit() -> void override {}
    auto rollback() -> void override
    {
        if (probe.failRollbackWithUnknownFailure)
        {
            probe.failRollbackWithUnknownFailure = false;
            throw UnknownInsertManyDriverFailure{};
        }
        if (probe.failRollback)
        {
            throw soci::soci_error{"injected rollback failure"};
        }
    }
    [[nodiscard]] auto get_dummy_from_table() const -> std::string override
    {
        return {};
    }
    [[nodiscard]] auto get_backend_name() const -> std::string override
    {
        return "insert-many-fault";
    }
    auto make_statement_backend() -> soci::details::statement_backend* override
    {
        return new InsertManyFaultStatement{probe};
    }
    auto make_rowid_backend() -> soci::details::rowid_backend* override
    {
        return nullptr;
    }
    auto make_blob_backend() -> soci::details::blob_backend* override
    {
        return nullptr;
    }

private:
    InsertManyFaultProbe& probe;
};

class InsertManyFaultFactory final : public soci::backend_factory
{
public:
    InsertManyFaultProbe* probe{};

    auto make_session(const soci::connection_parameters&) const -> soci::details::session_backend* override
    {
        return new InsertManyFaultSession{*probe};
    }
};

/**
 * @brief Keeps the PostgreSQL bind, limit and error-translation semantics while statements stay fake.
 */
class InsertManyFaultRuntime final : public orm::db::BackendRuntime
{
public:
    InsertManyFaultRuntime(const orm::db::BackendRuntime& delegateInit, InsertManyFaultProbe& probeInit)
        : delegate{delegateInit}, probe{probeInit}
    {
    }

    auto open(soci::session& session, orm::db::ConnectionConfigView) const -> void override
    {
        factory.probe = &probe;
        session.open(factory, "");
    }
    auto onConnect(soci::session&) const -> void override {}
    auto tableExists(soci::session& session, std::string_view table) const -> bool override
    {
        return delegate.tableExists(session, table);
    }
    auto limits(soci::session& session) const -> orm::db::BackendRuntimeLimits override
    {
        return delegate.limits(session);
    }
    auto normalizeAffectedRows(long long affectedRows) const -> std::size_t override
    {
        return delegate.normalizeAffectedRows(affectedRows);
    }
    auto bind(soci::values& values, std::string_view name, const orm::db::BoundValue& value) const -> void override
    {
        delegate.bind(values, name, value);
    }
    auto translateError(const soci::soci_error& error, orm::DatabaseErrorCode fallback,
                        std::string_view operation) const -> orm::DatabaseError override
    {
        return delegate.translateError(error, fallback, operation);
    }
    auto statementErrorInvalidatesTransaction(const soci::soci_error& error) const noexcept -> bool override
    {
        return delegate.statementErrorInvalidatesTransaction(error);
    }
    auto executeInsertIfAbsent(soci::session& session, orm::db::StatementView statement,
                               const orm::db::InsertIfAbsentSpec& spec) const -> std::size_t override
    {
        return delegate.executeInsertIfAbsent(session, statement, spec);
    }

private:
    const orm::db::BackendRuntime& delegate;
    InsertManyFaultProbe& probe;
    mutable InsertManyFaultFactory factory;
};

/**
 * @brief Command generator whose batch statement can fail before any SQL is executed.
 */
class ThrowingInsertManyGenerator final : public orm::db::CommandGenerator
{
public:
    explicit ThrowingInsertManyGenerator(InsertManyFaultProbe& probeInit) : probe{probeInit} {}

    auto createTable(orm::model::ModelView) const -> std::string override
    {
        return {};
    }
    auto dropTable(orm::model::ModelView) const -> std::string override
    {
        return {};
    }
    auto insert(orm::model::ModelView) const -> std::string override
    {
        return {};
    }
    auto insertMany(orm::model::ModelView, const orm::db::InsertManySpec&) const -> std::string override
    {
        if (probe.throwFromInsertMany)
        {
            throw UnknownInsertManyDriverFailure{};
        }
        return {};
    }
    auto select(orm::model::ModelView, const orm::db::SelectSpec&) const -> orm::db::SelectStatement override
    {
        return {};
    }
    auto update(orm::model::ModelView, const orm::db::UpdateSpec&) const -> orm::db::Statement override
    {
        return {};
    }
    auto remove(orm::model::ModelView, const orm::db::Predicate&) const -> orm::db::Statement override
    {
        return {};
    }

private:
    InsertManyFaultProbe& probe;
};

class InsertManyFaultBackend final : public orm::db::BackendProvider
{
private:
    orm::db::postgresql::PostgresqlBackend postgresql;
    std::unique_ptr<orm::db::CommandGenerator> customGenerator;

public:
    explicit InsertManyFaultBackend(InsertManyFaultProbe& probeInit,
                                    std::unique_ptr<orm::db::CommandGenerator> generator = nullptr)
        : postgresql{},
          customGenerator{std::move(generator)},
          dialectValue{postgresql.dialect()},
          runtimeStrategy{postgresql.runtime(), probeInit},
          capabilitiesValue{postgresql.capabilities()}
    {
    }

    auto type() const noexcept -> orm::db::BackendType override
    {
        return orm::db::BackendType::Postgres;
    }
    auto capabilities() const noexcept -> const orm::db::BackendCapabilities& override
    {
        return capabilitiesValue;
    }
    auto dialect() const noexcept -> const orm::db::SqlDialect& override
    {
        return dialectValue;
    }
    auto runtime() const noexcept -> const orm::db::BackendRuntime& override
    {
        return runtimeStrategy;
    }
    auto commandGenerator() const noexcept -> const orm::db::CommandGenerator& override
    {
        return customGenerator != nullptr ? *customGenerator : postgresql.commandGenerator();
    }

    const orm::db::SqlDialect& dialectValue;
    InsertManyFaultRuntime runtimeStrategy;
    orm::db::BackendCapabilities capabilitiesValue;
};

struct InsertManyFaultConfig
{
};
} // namespace

template <>
struct orm::db::ConnectionTraits<InsertManyFaultConfig>
{
    static constexpr auto backendType = orm::db::BackendType::Postgres;
};

namespace
{
/**
 * @brief Database bundle whose session and statements are deterministic doubles.
 */
class InsertManyFaultBundle
{
public:
    explicit InsertManyFaultBundle(InsertManyFaultProbe& probe,
                                   std::unique_ptr<orm::db::CommandGenerator> generator = nullptr)
    {
        auto factory = orm::db::CommandGeneratorFactory{std::vector<std::unique_ptr<orm::db::BackendProvider>>{}};
        factory.registerBackend(std::make_unique<InsertManyFaultBackend>(probe, std::move(generator)));
        connection = std::make_unique<orm::Database>(InsertManyFaultConfig{}, std::move(factory));
        database =
            std::make_unique<orm::OrmContext<InsertManySavepointSchema>>(connection->orm<InsertManySavepointSchema>());
        connection->disconnect();
    }

    auto connect() -> void
    {
        connection->connect(InsertManyFaultConfig{});
    }

    std::unique_ptr<orm::Database> connection;
    std::unique_ptr<orm::OrmContext<InsertManySavepointSchema>> database;
};

auto cleanupFailureMessage(const orm::DatabaseError& error) -> std::string
{
    return std::string{error.what()};
}
} // namespace

// These tests drive the insertMany transaction protocol through a session/statement double, so they need no live
// server but keep the PostgreSQL bind, limit and error-translation semantics.
TEST(InsertManySavepointCleanupTest, savepointRecoveryFailurePreservesTheOriginalBatchError)
{
    InsertManyFaultProbe probe;
    InsertManyFaultBundle bundle{probe};
    bundle.connect();
    bundle.connection->beginTransaction();

    const auto rows = makeRows(3, 1);
    probe.failChunk = true;
    probe.failSavepointRecovery = true;

    try
    {
        bundle.database->insertMany(std::span<const models::ModelWithId>{rows});
        FAIL() << "Expected orm::DatabaseError";
    }
    catch (const orm::DatabaseError& error)
    {
        EXPECT_EQ(error.getCode(), orm::DatabaseErrorCode::Statement);
        EXPECT_EQ(error.getOperation(), "insert many");
        EXPECT_THAT(cleanupFailureMessage(error), ::testing::HasSubstr("; savepoint cleanup failed: "));
    }

    // The failed cleanup must leave the caller transaction marked as failed instead of pretending it recovered.
    EXPECT_THROW(bundle.connection->commitTransaction(), orm::DatabaseError);

    probe.failSavepointRecovery = false;
    EXPECT_NO_THROW(bundle.connection->rollbackTransaction());
}

TEST(InsertManySavepointCleanupTest, unknownSavepointRecoveryFailurePreservesTheOriginalBatchError)
{
    InsertManyFaultProbe probe;
    InsertManyFaultBundle bundle{probe};
    bundle.connect();
    bundle.connection->beginTransaction();

    const auto rows = makeRows(3, 1);
    probe.failChunk = true;
    probe.failSavepointRecovery = true;
    probe.failSavepointRecoveryWithUnknownFailure = true;

    try
    {
        bundle.database->insertMany(std::span<const models::ModelWithId>{rows});
        FAIL() << "Expected orm::DatabaseError";
    }
    catch (const orm::DatabaseError& error)
    {
        EXPECT_EQ(error.getCode(), orm::DatabaseErrorCode::Statement);
        EXPECT_THAT(cleanupFailureMessage(error),
                    ::testing::HasSubstr("; savepoint cleanup failed: unknown savepoint cleanup failure"));
    }

    EXPECT_THROW(bundle.connection->commitTransaction(), orm::DatabaseError);

    probe.failSavepointRecovery = false;
    EXPECT_NO_THROW(bundle.connection->rollbackTransaction());
}

TEST(InsertManySavepointCleanupTest, ownTransactionRollbackFailurePreservesTheOriginalBatchError)
{
    InsertManyFaultProbe probe;
    InsertManyFaultBundle bundle{probe};
    bundle.connect();

    const auto rows = makeRows(3, 1);
    probe.failChunk = true;
    probe.failRollback = true;

    try
    {
        bundle.database->insertMany(std::span<const models::ModelWithId>{rows});
        FAIL() << "Expected orm::DatabaseError";
    }
    catch (const orm::DatabaseError& error)
    {
        EXPECT_EQ(error.getCode(), orm::DatabaseErrorCode::Statement);
        EXPECT_THAT(cleanupFailureMessage(error), ::testing::HasSubstr("; transaction rollback failed: "));
    }

    probe.failRollback = false;
    EXPECT_FALSE(bundle.connection->isConnected());
}

TEST(InsertManySavepointCleanupTest, unknownOwnTransactionRollbackFailurePreservesTheOriginalBatchError)
{
    InsertManyFaultProbe probe;
    InsertManyFaultBundle bundle{probe};
    bundle.connect();

    const auto rows = makeRows(3, 1);
    probe.failChunk = true;
    probe.failRollbackWithUnknownFailure = true;

    try
    {
        bundle.database->insertMany(std::span<const models::ModelWithId>{rows});
        FAIL() << "Expected orm::DatabaseError";
    }
    catch (const orm::DatabaseError& error)
    {
        EXPECT_EQ(error.getCode(), orm::DatabaseErrorCode::Statement);
        EXPECT_THAT(cleanupFailureMessage(error),
                    ::testing::HasSubstr("; transaction rollback failed: unknown transaction rollback failure"));
    }

    EXPECT_FALSE(bundle.connection->isConnected());
}

TEST(InsertManySavepointCleanupTest, unknownBatchFailureIsRethrownWhenRollbackAlsoFails)
{
    InsertManyFaultProbe probe;
    probe.throwFromInsertMany = true;
    InsertManyFaultBundle bundle{probe, std::make_unique<ThrowingInsertManyGenerator>(probe)};
    bundle.connect();

    const auto rows = makeRows(3, 1);
    probe.failRollback = true;

    EXPECT_THROW(bundle.database->insertMany(std::span<const models::ModelWithId>{rows}),
                 UnknownInsertManyDriverFailure);

    probe.failRollback = false;
    EXPECT_FALSE(bundle.connection->isConnected());
}
