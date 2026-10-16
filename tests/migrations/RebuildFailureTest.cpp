module;

#include <array>
#include <chrono>
#include <filesystem>
#include <string>
#include <utility>

#include "tests/UnitTestPrelude.hpp"

// SOCI distributions expose either full C declarations or opaque SQLite types.
// clang-format off
#include "soci/sqlite3/soci-sqlite3.h"
#include <sqlite3.h>
// clang-format on

module orm;

import :internal;
import :migrations_internal;

namespace sqlite_api
{
}

namespace
{
using namespace sqlite_api;
using namespace orm::migrations;
using Backend = orm::db::BackendType;
using Access = detail::DatabaseAccess;

template <typename Handle, typename Result, typename Native, typename... Args>
auto nativeSqliteConnection(Handle* handle, Result (*)(Native*, Args...)) -> Native*
{
    return reinterpret_cast<Native*>(handle);
}

enum class Fault
{
    None,
    BeforeCommit,
    AfterCommit,
    Restore,
};

struct FaultControl
{
    Fault armed{Fault::None};
    int readCommits{};
    int writeCommits{};
    int realWriteCommits{};
    int rollbacks{};
    int injections{};
    int restorations{};
    int restorationFailures{};
    bool failRestore{};
    bool startTransactionAfterInspection{};
};

class FaultOperations final : public detail::SessionOperations
{
public:
    explicit FaultOperations(FaultControl& control) : control_{control} {}

    auto commit(soci::session& session, Version version) const -> void override
    {
        // Inspection commits carry version zero. Inject at the boundary of the
        // generated rebuild's actual SOCI commit, independently of its driver ABI.
        if (version != 2)
        {
            ++control_.readCommits;
            detail::SessionOperations::commit(session, version);
            if (std::exchange(control_.startTransactionAfterInspection, false))
                session.begin();
            return;
        }
        ++control_.writeCommits;
        const auto fault = std::exchange(control_.armed, Fault::None);
        if (fault != Fault::None)
            ++control_.injections;
        if (fault == Fault::BeforeCommit)
            throw soci::soci_error{"Injected write-commit failure before COMMIT"};
        detail::SessionOperations::commit(session, version);
        ++control_.realWriteCommits;
        if (fault == Fault::AfterCommit)
            throw soci::soci_error{"Injected write-commit failure after COMMIT"};
        if (fault == Fault::Restore)
            control_.failRestore = true;
    }

    auto rollback(soci::session& session) const -> void override
    {
        ++control_.rollbacks;
        detail::SessionOperations::rollback(session);
    }

    auto restoreSqliteForeignKeys(soci::session& session, int previousValue) const -> void override
    {
        ++control_.restorations;
        if (std::exchange(control_.failRestore, false))
        {
            ++control_.restorationFailures;
            // SQLite ignores foreign_keys changes inside a transaction. Start
            // a read transaction after the real write commit, so the default
            // restoration operation detects the actual setting mismatch.
            session.begin();
        }
        detail::SessionOperations::restoreSqliteForeignKeys(session, previousValue);
    }

private:
    FaultControl& control_;
};

class DatabaseFile
{
public:
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("orm_rebuild_failure_" + std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()) +
         ".db");
    ~DatabaseFile()
    {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
    auto url() const -> std::string
    {
        return "sqlite3://" + path.string();
    }
};

auto beforeSnapshot() -> SchemaSnapshot
{
    auto snapshot = emptySnapshot(std::array{Backend::Sqlite});
    PhysicalTable table;
    table.name = "failure_identity";
    table.columns = {{.name = "id",
                      .logicalType = orm::model::ColumnType::Int,
                      .sqlType = "INTEGER",
                      .nullable = false,
                      .maxStringLength = {},
                      .autoIncrement = true}};
    table.primaryKey = {"id"};
    table.primaryKeyName = "failure_identity_pk";
    snapshot.backends.at(Backend::Sqlite).tables.push_back(std::move(table));
    detail::canonicalize(snapshot);
    return snapshot;
}

auto rebuildCatalog() -> Catalog
{
    const auto before = beforeSnapshot();
    const auto initial = generate(1, "initial", compare(emptySnapshot(std::array{Backend::Sqlite}), before)).migration;
    auto after = before;
    after.backends.at(Backend::Sqlite)
        .tables.front()
        .columns.push_back({.name = "label",
                            .logicalType = orm::model::ColumnType::String,
                            .sqlType = "TEXT",
                            .nullable = false,
                            .maxStringLength = {},
                            .autoIncrement = false});
    detail::canonicalize(after);
    GenerationDecisions decisions;
    decisions.backfills.push_back({.table = "failure_identity",
                                   .column = "label",
                                   .constant = GenerationLiteral{std::string{"backfilled"}},
                                   .sourceColumn = {},
                                   .expressions = {}});
    const auto second = generate(2, "required_label", compare(before, after, decisions), decisions).migration;
    return Catalog{initial, second};
}

auto setup(orm::Database& database, const DatabaseFile& file, const Catalog& catalog) -> void
{
    database.connect(file.url());
    Runner{database, catalog}.up(1);
    auto& session = Access::session(database);
    session << "INSERT INTO failure_identity(id) VALUES(1)";
    session << "INSERT INTO failure_identity(id) VALUES(90)";
    session << "DELETE FROM failure_identity WHERE id=90";
}

auto assertReconnectedState(orm::Database& database, const DatabaseFile& file, const Catalog& catalog,
                            bool committed) -> void
{
    database.connect(file.url());
    const auto status = Runner{database, catalog}.status();
    EXPECT_TRUE(status.isValid());
    EXPECT_EQ(status.currentVersion, committed ? 2 : 1);
    EXPECT_EQ(status.applied.size(), committed ? 2U : 1U);
    auto& session = Access::session(database);
    int rows{}, labels{}, foreignKeys{};
    session << "SELECT COUNT(*) FROM failure_identity", soci::into(rows);
    session << "SELECT COUNT(*) FROM pragma_table_info('failure_identity') WHERE name='label'", soci::into(labels);
    session << "PRAGMA foreign_keys", soci::into(foreignKeys);
    EXPECT_EQ(rows, 1);
    EXPECT_EQ(labels, committed ? 1 : 0);
    EXPECT_EQ(foreignKeys, 1);
    EXPECT_FALSE(Access::active(database));
    if (committed)
    {
        std::string label;
        session << "SELECT label FROM failure_identity WHERE id=1", soci::into(label);
        EXPECT_EQ(label, "backfilled");
        session << "INSERT INTO failure_identity(label) VALUES('next')";
    }
    else
        session << "INSERT INTO failure_identity DEFAULT VALUES";
    long long highwater{};
    session << "SELECT MAX(id) FROM failure_identity", soci::into(highwater);
    EXPECT_EQ(highwater, 91);
}

auto exerciseCommitFault(Fault fault, bool committed) -> void
{
    DatabaseFile file;
    FaultControl control;
    FaultOperations operations{control};
    orm::Database database;
    const auto catalog = rebuildCatalog();
    setup(database, file, catalog);
    control.armed = fault;
    try
    {
        detail::RunnerAccess::withSessionOperations(database, catalog, operations).up();
        FAIL() << "Expected an uncertain write-commit result";
    }
    catch (const MigrationError& error)
    {
        EXPECT_EQ(error.getCode(), ErrorCode::CommitUncertain) << error.what();
        EXPECT_EQ(error.getVersion(), 2);
        EXPECT_EQ(error.getBackendType(), Backend::Sqlite);
    }
    EXPECT_FALSE(database.isConnected());
    EXPECT_FALSE(Access::active(database));
    EXPECT_EQ(control.injections, 1);
    EXPECT_EQ(control.writeCommits, 1);
    EXPECT_EQ(control.realWriteCommits, committed ? 1 : 0);
    EXPECT_GE(control.readCommits, 1);
    // Uncertainty is resolved by closing/reconnecting, never by issuing a
    // guessed ROLLBACK after COMMIT might already have reached the database.
    EXPECT_EQ(control.rollbacks, 0);
    assertReconnectedState(database, file, catalog, committed);
    EXPECT_EQ(control.rollbacks, 0);
}
} // namespace

TEST(MigrationRebuildFailureTest, FailureBeforeCommitDisconnectsAndCloseRollsBackOnlyTheCurrentMigration)
{
    exerciseCommitFault(Fault::BeforeCommit, false);
}

TEST(MigrationRebuildFailureTest, NativeSessionPreparationFailurePreservesHistoryAndOwnership)
{
    DatabaseFile file;
    orm::Database database;
    const auto catalog = rebuildCatalog();
    setup(database, file, catalog);
    auto& session = Access::session(database);
    auto* handle = nativeSqliteConnection(static_cast<soci::sqlite3_session_backend*>(session.get_backend())->conn_,
                                          sqlite3_set_authorizer);
    int denied{};
    {
        const auto installed = sqlite3_set_authorizer(
            handle,
            [](void* context, int action, const char* name, const char*, const char*, const char*)
            {
                if (action == SQLITE_PRAGMA && name != nullptr && std::string_view{name} == "foreign_keys")
                {
                    ++*static_cast<int*>(context);
                    return SQLITE_DENY;
                }
                return SQLITE_OK;
            },
            &denied);
        ASSERT_EQ(installed, SQLITE_OK);
        struct RestoreAuthorization
        {
            decltype(handle) connection;
            ~RestoreAuthorization()
            {
                sqlite3_set_authorizer(connection, nullptr, nullptr);
            }
        } restore{handle};
        try
        {
            Runner{database, catalog}.up();
            FAIL() << "Expected the real SQLite authorization failure";
        }
        catch (const MigrationError& error)
        {
            EXPECT_EQ(error.getCode(), ErrorCode::Execution);
            EXPECT_EQ(error.getVersion(), 2);
            EXPECT_EQ(error.getBackendType(), Backend::Sqlite);
        }
    }
    EXPECT_EQ(denied, 1);
    EXPECT_TRUE(database.isConnected());
    EXPECT_FALSE(Access::active(database));
    EXPECT_EQ(Runner(database, catalog).status().currentVersion, 1);
    Runner{database, catalog}.up();
    EXPECT_EQ(Runner(database, catalog).status().currentVersion, 2);
}

TEST(MigrationRebuildFailureTest, FailureAfterRealCommitDisconnectsAndKeepsTheCommittedHistory)
{
    exerciseCommitFault(Fault::AfterCommit, true);
}

TEST(MigrationRebuildFailureTest, RestorationFailureIsDistinctFromCommitUncertaintyAndKeepsCommittedHistory)
{
    DatabaseFile file;
    FaultControl control;
    FaultOperations operations{control};
    orm::Database database;
    const auto catalog = rebuildCatalog();
    setup(database, file, catalog);
    control.armed = Fault::Restore;
    try
    {
        detail::RunnerAccess::withSessionOperations(database, catalog, operations).up();
        FAIL() << "Expected session-restoration failure after a successful write commit";
    }
    catch (const MigrationError& error)
    {
        EXPECT_EQ(error.getCode(), ErrorCode::SessionRestoration) << error.what();
        EXPECT_EQ(error.getVersion(), 2);
        EXPECT_EQ(error.getBackendType(), Backend::Sqlite);
    }
    EXPECT_FALSE(database.isConnected());
    EXPECT_FALSE(Access::active(database));
    EXPECT_EQ(control.injections, 1);
    EXPECT_EQ(control.realWriteCommits, 1);
    EXPECT_EQ(control.rollbacks, 0);
    EXPECT_EQ(control.restorationFailures, 1);
    assertReconnectedState(database, file, catalog, true);
    EXPECT_EQ(control.rollbacks, 0);
}

TEST(MigrationRebuildFailureTest, SuccessfulCommitRestoresForeignKeysAndReleasesMigrationOwnership)
{
    DatabaseFile file;
    FaultControl control;
    FaultOperations operations{control};
    orm::Database database;
    const auto catalog = rebuildCatalog();
    setup(database, file, catalog);
    auto runner = detail::RunnerAccess::withSessionOperations(database, catalog, operations);
    ASSERT_NO_THROW(runner.up());
    EXPECT_TRUE(database.isConnected());
    EXPECT_FALSE(Access::active(database));
    EXPECT_EQ(control.injections, 0);
    EXPECT_EQ(control.realWriteCommits, 1);
    EXPECT_EQ(control.rollbacks, 0);
    EXPECT_EQ(control.restorations, 1);
    int foreignKeys{};
    Access::session(database) << "PRAGMA foreign_keys", soci::into(foreignKeys);
    EXPECT_EQ(foreignKeys, 1);
    EXPECT_EQ((Runner{database, catalog}.status().currentVersion), 2);
    database.disconnect();
    assertReconnectedState(database, file, catalog, true);
}

TEST(MigrationRebuildFailureTest, HeapSessionDependencyIsBorrowedAndReleasedAfterTheRunner)
{
    DatabaseFile file;
    orm::Database database;
    const auto catalog = rebuildCatalog();
    setup(database, file, catalog);
    auto operations = std::make_unique<detail::SessionOperations>();
    {
        auto runner = detail::RunnerAccess::withSessionOperations(database, catalog, *operations);
        ASSERT_NO_THROW(runner.up());
        EXPECT_EQ(runner.status().currentVersion, 2);
    }
    operations.reset();
    EXPECT_TRUE(database.isConnected());
    EXPECT_FALSE(Access::active(database));
    EXPECT_EQ((Runner{database, catalog}.status().currentVersion), 2);
}

TEST(MigrationRebuildFailureTest, CleanupRestorationFailureDisconnectsWithoutMaskingTheSourceDrift)
{
    DatabaseFile file;
    FaultControl control;
    FaultOperations operations{control};
    orm::Database database;
    const auto catalog = rebuildCatalog();
    setup(database, file, catalog);
    Access::session(database) << "CREATE INDEX unmanaged_failure_index ON failure_identity(id)";
    control.failRestore = true;
    try
    {
        detail::RunnerAccess::withSessionOperations(database, catalog, operations).up();
        FAIL() << "Expected source drift, even when session restoration also fails";
    }
    catch (const MigrationError& error)
    {
        EXPECT_EQ(error.getCode(), ErrorCode::InvalidSchema) << error.what();
        EXPECT_NE(std::string{error.what()}.find("Unmanaged index"), std::string::npos);
        EXPECT_EQ(error.getVersion(), 2);
    }
    EXPECT_FALSE(database.isConnected());
    EXPECT_FALSE(Access::active(database));
    EXPECT_EQ(control.restorationFailures, 1);
    EXPECT_EQ(control.rollbacks, 1);
    EXPECT_EQ(control.realWriteCommits, 0);
    assertReconnectedState(database, file, catalog, false);
}

TEST(MigrationRebuildFailureTest, CannotDisableForeignKeysWhenTheSessionBecomesBusyAfterInspection)
{
    DatabaseFile file;
    FaultControl control;
    FaultOperations operations{control};
    orm::Database database;
    const auto catalog = rebuildCatalog();
    setup(database, file, catalog);
    control.startTransactionAfterInspection = true;
    try
    {
        detail::RunnerAccess::withSessionOperations(database, catalog, operations).up();
        FAIL() << "An active SQL transaction must prevent foreign-key disabling";
    }
    catch (const MigrationError& error)
    {
        EXPECT_EQ(error.getCode(), ErrorCode::InvalidSchema) << error.what();
        EXPECT_NE(std::string{error.what()}.find("Cannot disable SQLite foreign keys"), std::string::npos);
        EXPECT_EQ(error.getVersion(), 2);
    }
    EXPECT_TRUE(database.isConnected());
    EXPECT_FALSE(Access::active(database));
    EXPECT_EQ(control.realWriteCommits, 0);
    int foreignKeys{};
    Access::session(database) << "PRAGMA foreign_keys", soci::into(foreignKeys);
    EXPECT_EQ(foreignKeys, 1);
    Access::session(database).rollback();
    EXPECT_EQ((Runner{database, catalog}.status().currentVersion), 1);
    ASSERT_NO_THROW((Runner{database, catalog}.up()));
    EXPECT_EQ((Runner{database, catalog}.status().currentVersion), 2);
}
