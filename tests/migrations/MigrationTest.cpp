module;

#include <filesystem>
#include <fstream>
#include <thread>

#include "tests/UnitTestPrelude.hpp"

module orm;

import :internal;
import :test_support;
import :migrations_internal;

using namespace orm::migrations;
using namespace orm::test::fixtures;

namespace
{
auto migration(Version version, std::string up, std::optional<std::string> down = std::nullopt) -> Migration
{
    SqlMigration scripts{.up = std::move(up), .down = std::move(down)};
    return Migration{.version = version,
                     .name = "step_" + std::to_string(version),
                     .scripts = {{orm::db::BackendType::Sqlite, scripts}, {orm::db::BackendType::Postgres, scripts}}};
}
auto initialCatalog() -> Catalog
{
    return Catalog{
        migration(10,
                  "CREATE TABLE migration_users (id INTEGER PRIMARY KEY, name TEXT NOT NULL); INSERT INTO "
                  "migration_users VALUES (1,'Ann');",
                  "DROP TABLE migration_users;"),
        migration(
            30,
            "ALTER TABLE migration_users ADD COLUMN email TEXT; UPDATE migration_users SET email='ann@example.com';",
            "ALTER TABLE migration_users DROP COLUMN email;")};
}
template <typename F>
auto expectMigrationError(F&& operation, ErrorCode code) -> void
{
    try
    {
        operation();
        FAIL() << "Expected MigrationError";
    }
    catch (const MigrationError& error)
    {
        EXPECT_EQ(error.getCode(), code) << error.what();
    }
}
class TempDirectory
{
public:
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("orm_migrations_" + std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    TempDirectory()
    {
        std::filesystem::create_directories(path);
    }
    ~TempDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
    auto write(const std::filesystem::path& relative, const std::string& sql) const -> void
    {
        std::filesystem::create_directories((path / relative).parent_path());
        std::ofstream stream(path / relative, std::ios::binary);
        stream << sql;
    }
};
} // namespace

class MigrationTest : public DatabaseTest<models::Schema>
{
public:
    auto session() -> soci::session&
    {
        return orm::migrations::detail::DatabaseAccess::session(connection);
    }
    auto users() -> int
    {
        int count{};
        session() << "SELECT COUNT(*) FROM migration_users", soci::into(count);
        return count;
    }
    auto historyTable() -> std::string
    {
        return orm::migrations::detail::DatabaseAccess::runtime(connection).migrationTable(session());
    }
};

TEST_P(MigrationTest, ReadOnlyCommandsDoNotCreateHistory)
{
    Runner runner{connection, initialCatalog()};
    auto status = runner.status();
    EXPECT_TRUE(status.isValid());
    EXPECT_EQ(status.currentVersion, 0);
    EXPECT_EQ(status.pending.size(), 2);
    runner.validate();
    auto plan = runner.preview(Direction::Up);
    EXPECT_EQ(plan.from, 0);
    EXPECT_EQ(plan.to, 30);
    ASSERT_EQ(plan.migrations.size(), 2);
    const auto table = historyTable();
    EXPECT_THROW(session() << "SELECT version FROM " + table, soci::soci_error);
}

TEST_P(MigrationTest, AppliesWholeScriptsAndRetainsData)
{
    Runner runner{connection, initialCatalog()};
    runner.up(10);
    EXPECT_EQ(users(), 1);
    runner.up();
    EXPECT_EQ(runner.status().currentVersion, 30);
    std::string email;
    session() << "SELECT email FROM migration_users WHERE id=1", soci::into(email);
    EXPECT_EQ(email, "ann@example.com");
    runner.up();
    EXPECT_EQ(users(), 1);
    EXPECT_EQ(runner.status().applied.size(), 2);
    auto plan = runner.preview(Direction::Down, 0);
    ASSERT_EQ(plan.migrations.size(), 2);
    EXPECT_EQ(plan.migrations[0].version, 30);
    runner.down(10);
    EXPECT_EQ(users(), 1);
    runner.down(0);
    EXPECT_TRUE(runner.status().applied.empty());
    runner.up();
    EXPECT_EQ(users(), 1);
}

TEST_P(MigrationTest, FailureRollsBackCurrentMigrationAndCanRetry)
{
    Runner broken{
        connection,
        Catalog{migration(1, "CREATE TABLE migration_users(id INTEGER PRIMARY KEY);", "DROP TABLE migration_users;"),
                migration(2, "ALTER TABLE migration_users ADD COLUMN extra TEXT; INSERT INTO migration_users "
                             "VALUES(1,'kept?'); INSERT INTO nonexistent_table VALUES(1);")}};
    try
    {
        broken.up();
        FAIL();
    }
    catch (const MigrationError& error)
    {
        EXPECT_EQ(error.getVersion(), 2);
        EXPECT_EQ(error.getCode(), ErrorCode::Execution);
        EXPECT_TRUE(error.getNativeCode());
    }
    EXPECT_EQ(broken.status().currentVersion, 1);
    EXPECT_EQ(users(), 0);
    Runner corrected{
        connection,
        Catalog{
            migration(1, "CREATE TABLE migration_users(id INTEGER PRIMARY KEY);", "DROP TABLE migration_users;"),
            migration(
                2, "ALTER TABLE migration_users ADD COLUMN extra TEXT; INSERT INTO migration_users VALUES(1,'ok');")}};
    corrected.up();
    EXPECT_EQ(users(), 1);
    EXPECT_EQ(corrected.status().currentVersion, 2);
}

TEST_P(MigrationTest, FailureOfFirstMigrationLeavesNoHistory)
{
    Runner broken{connection,
                  Catalog{migration(1, "CREATE TABLE migration_users(id INTEGER); INSERT INTO missing VALUES(1);")}};
    EXPECT_THROW(broken.up(), MigrationError);
    EXPECT_EQ(broken.status().currentVersion, 0);
    Runner corrected{connection, Catalog{migration(1, "CREATE TABLE migration_users(id INTEGER);")}};
    EXPECT_NO_THROW(corrected.up());
}

TEST_P(MigrationTest, ChecksumsIncludeDownAndHistoryMustBePrefix)
{
    Runner original{connection, initialCatalog()};
    original.up(10);
    std::vector<Migration> changed;
    const auto catalog = initialCatalog();
    changed.assign(catalog.migrations().begin(), catalog.migrations().end());
    changed[0].scripts[GetParam().type].down = "DROP TABLE IF EXISTS migration_users;";
    Runner edited{connection, Catalog{changed}};
    EXPECT_FALSE(edited.status().isValid());
    expectMigrationError([&] { edited.up(); }, ErrorCode::ChecksumMismatch);
    changed[0].name = "renamed";
    Runner renamed{connection, Catalog{changed}};
    expectMigrationError([&] { renamed.validate(); }, ErrorCode::InvalidHistory);
    Runner removed{connection, Catalog{migration(30, "SELECT 1;")}};
    expectMigrationError([&] { removed.up(); }, ErrorCode::InvalidHistory);
    EXPECT_THROW(session() << "UPDATE " + historyTable() + " SET baseline=2", soci::soci_error);
}

TEST_P(MigrationTest, MissingDownIsRejectedBeforeAnyChanges)
{
    Runner runner{connection,
                  Catalog{migration(1, "CREATE TABLE migration_users(id INTEGER);"),
                          migration(2, "INSERT INTO migration_users VALUES(1);", "DELETE FROM migration_users;")}};
    runner.up();
    expectMigrationError([&] { runner.down(0); }, ErrorCode::MissingDown);
    EXPECT_EQ(users(), 1);
    EXPECT_EQ(runner.status().currentVersion, 2);
}

TEST_P(MigrationTest, BaselineRecordsPrefixWithoutRunningSql)
{
    session() << "CREATE TABLE migration_users(id INTEGER PRIMARY KEY,name TEXT NOT NULL);";
    session() << "INSERT INTO migration_users VALUES(1,'Ann');";
    Runner runner{connection, initialCatalog()};
    runner.baseline(10);
    EXPECT_EQ(users(), 1);
    EXPECT_EQ(runner.status().baselineVersion, 10);
    runner.up();
    EXPECT_EQ(runner.status().currentVersion, 30);
    runner.down(10);
    expectMigrationError([&] { runner.down(0); }, ErrorCode::BaselineBoundary);
    expectMigrationError([&] { runner.baseline(10); }, ErrorCode::InvalidTarget);
}

TEST_P(MigrationTest, InvalidTargetsAndBusySessionsAreRejected)
{
    Runner runner{connection, initialCatalog()};
    expectMigrationError([&] { runner.up(2); }, ErrorCode::InvalidTarget);
    expectMigrationError([&] { runner.down(30); }, ErrorCode::InvalidTarget);
    expectMigrationError([&] { runner.baseline(0); }, ErrorCode::InvalidTarget);
    expectMigrationError([&] { (void)runner.preview(Direction::Up, -1); }, ErrorCode::InvalidTarget);
    connection.beginTransaction();
    expectMigrationError([&] { (void)runner.status(); }, ErrorCode::ActiveTransaction);
    connection.rollbackTransaction();
    runner.up();
    expectMigrationError([&] { runner.up(10); }, ErrorCode::InvalidTarget);
    connection.disconnect();
    expectMigrationError([&] { runner.up(); }, ErrorCode::NotConnected);
}

TEST_P(MigrationTest, UnsupportedAndMissingBackendScripts)
{
    const auto other =
        GetParam().type == orm::db::BackendType::Sqlite ? orm::db::BackendType::Postgres : orm::db::BackendType::Sqlite;
    Runner runner{connection, Catalog{Migration{1, "only_other", {{other, {"SELECT 1;", std::nullopt}}}}}};
    expectMigrationError([&] { runner.up(); }, ErrorCode::InvalidCatalog);
}

TEST_P(MigrationTest, TransactionControlAndHistoryEditsRollBack)
{
    for (const auto& sql : {"CREATE TABLE migration_users(id INTEGER); COMMIT;", "BEGIN; SELECT 1;", "SAVEPOINT bad;",
                            "DELETE FROM _orm_migrations;"})
    {
        Runner runner{connection, Catalog{migration(1, sql)}};
        EXPECT_THROW(runner.up(), MigrationError);
        EXPECT_EQ((Runner{connection, initialCatalog()}.status().currentVersion), 0);
    }
}

TEST_P(MigrationTest, QuotesCommentsAndProceduralBodies)
{
    std::string sql = "-- COMMIT;\n/* ROLLBACK; */ CREATE TABLE migration_users(id INTEGER PRIMARY KEY,name TEXT); "
                      "INSERT INTO migration_users VALUES(1,'a;''b');";
    if (GetParam().type == orm::db::BackendType::Sqlite)
    {
        sql += "CREATE TRIGGER migration_trigger AFTER INSERT ON migration_users BEGIN UPDATE migration_users SET "
               "name='trigger;done' WHERE id=NEW.id; END; INSERT INTO migration_users VALUES(2,'new');";
    }
    else
    {
        sql += "CREATE FUNCTION migration_function() RETURNS integer LANGUAGE plpgsql AS $body$ BEGIN RETURN 42; END; "
               "$body$; DO $$ BEGIN INSERT INTO migration_users VALUES(2,'function;done'); END; $$;";
    }
    Runner runner{connection, Catalog{migration(1, sql)}};
    runner.up();
    EXPECT_EQ(users(), 2);
}

TEST_P(MigrationTest, LocksRejectACompetingSession)
{
    TempDirectory directory;
    orm::Database competitor;
    if (GetParam().type == orm::db::BackendType::Sqlite)
    {
        connection.disconnect();
        const auto url = "sqlite3://" + (directory.path / "lock.db").string();
        connection.connect(url);
        competitor.connect(url);
    }
    else
    {
        competitor.connect(GetParam().type, testConnectionString());
    }
    auto& runtime = orm::migrations::detail::DatabaseAccess::runtime(connection);
    const auto table = runtime.migrationTable(session());
    runtime.beginMigration(session(), table);
    Runner second{competitor, initialCatalog()};
    expectMigrationError([&] { second.up(); }, ErrorCode::LockUnavailable);
    session().rollback();
    EXPECT_NO_THROW(second.up());
}

INSTANTIATE_TEST_SUITE_P(DatabaseTest, MigrationTest, conformanceBackendTestConfigs, backendTestName);

TEST_P(MigrationTest, EmptyCatalogAndExplicitNoOpDown)
{
    Runner empty{connection, Catalog{}};
    EXPECT_TRUE(empty.status().pending.empty());
    empty.up();
    empty.down(0);
    Runner runner{connection, Catalog{migration(9223372036854775807LL, "-- only a comment", "")}};
    runner.up();
    EXPECT_EQ(runner.status().currentVersion, 9223372036854775807LL);
    runner.down(0);
    EXPECT_EQ(runner.status().currentVersion, 0);
}

TEST_P(MigrationTest, DownFailurePreservesCurrentMigrationAndEarlierHistory)
{
    Runner runner{
        connection,
        Catalog{migration(1, "CREATE TABLE migration_users(id INTEGER);", "DROP TABLE migration_users;"),
                migration(2, "INSERT INTO migration_users VALUES(1);",
                          "DELETE FROM migration_users; SELECT * FROM missing;"),
                migration(3, "INSERT INTO migration_users VALUES(2);", "DELETE FROM migration_users WHERE id=2;")}};
    runner.up();
    expectMigrationError([&] { runner.down(0); }, ErrorCode::Execution);
    EXPECT_EQ(runner.status().currentVersion, 2);
    EXPECT_EQ(users(), 1);
}

TEST_P(MigrationTest, InvalidHistoryFlagsAndBaselineOrder)
{
    Runner runner{connection, initialCatalog()};
    runner.up();
    session() << "UPDATE " + historyTable() + " SET baseline=1 WHERE version=30";
    expectMigrationError([&] { runner.validate(); }, ErrorCode::InvalidHistory);
    session() << "UPDATE " + historyTable() + " SET baseline=0,backend=-1";
    expectMigrationError([&] { runner.validate(); }, ErrorCode::InvalidHistory);
}

TEST_P(MigrationTest, MalformedHistoryColumnTypesAndFlags)
{
    session() << "CREATE TABLE _orm_migrations(version BIGINT,name TEXT,backend TEXT,checksum TEXT,applied_at "
                 "TEXT,baseline INTEGER)";
    session() << "INSERT INTO _orm_migrations VALUES(10,'step_10','bad','bad','now',0)";
    Runner runner{connection, initialCatalog()};
    expectMigrationError([&] { (void)runner.status(); }, ErrorCode::InvalidHistory);
    session() << "DROP TABLE _orm_migrations";
    session() << "CREATE TABLE _orm_migrations(version BIGINT,name TEXT,backend INTEGER,checksum TEXT,applied_at "
                 "TEXT,baseline INTEGER)";
    session() << "INSERT INTO _orm_migrations VALUES(10,'step_10',0,'bad','now',2)";
    expectMigrationError([&] { (void)runner.status(); }, ErrorCode::InvalidHistory);
}

TEST_P(MigrationTest, PostgreSqlScriptsCannotChangeSessionsOrCorruptHistory)
{
    if (GetParam().type != orm::db::BackendType::Postgres)
    {
        GTEST_SKIP();
    }
    for (const auto sql :
         {"SELECT set_config('search_path','pg_catalog',false);",
          "CREATE TABLE migration_users(id INTEGER); CREATE INDEX CONCURRENTLY bad ON migration_users(id);"})
    {
        Runner runner{connection, Catalog{migration(1, sql)}};
        EXPECT_THROW(runner.up(), MigrationError);
    }
    Runner initial{connection, initialCatalog()};
    initial.up(10);
    auto entries = std::vector<Migration>{initialCatalog().migrations()[0],
                                          migration(30, "DO $$ BEGIN DELETE FROM _orm_migrations; END $$;")};
    Runner deleted{connection, Catalog{entries}};
    expectMigrationError([&] { deleted.up(); }, ErrorCode::InvalidHistory);
    EXPECT_EQ(initial.status().currentVersion, 10);
    entries[1] = migration(30, "DO $$ BEGIN UPDATE _orm_migrations SET name='changed'; END $$;");
    Runner renamed{connection, Catalog{entries}};
    expectMigrationError([&] { renamed.up(); }, ErrorCode::InvalidHistory);
    session() << "SET standard_conforming_strings=off";
    expectMigrationError([&] { initial.up(); }, ErrorCode::InvalidSql);
    session() << "SET standard_conforming_strings=on";
    session() << "SET search_path=nonexistent_migration_schema";
    expectMigrationError([&] { (void)initial.status(); }, ErrorCode::InvalidHistory);
}

TEST(MigrationRunnerTest, FailedCommitDisconnectsAndRequiresReinspection)
{
    TempDirectory directory;
    const auto url = "sqlite3://" + (directory.path / "commit.db").string();
    orm::Database database;
    database.connect(url);
    Runner runner{database, Catalog{migration(
                                1, "PRAGMA defer_foreign_keys=ON; CREATE TABLE parent(id INTEGER PRIMARY KEY); CREATE "
                                   "TABLE child(id INTEGER REFERENCES parent(id)); INSERT INTO child VALUES(1);")}};
    try
    {
        runner.up();
        FAIL();
    }
    catch (const MigrationError& error)
    {
        EXPECT_EQ(error.getCode(), ErrorCode::CommitUncertain);
        EXPECT_EQ(error.getVersion(), 1);
        EXPECT_EQ(error.getBackendType(), orm::db::BackendType::Sqlite);
    }
    EXPECT_FALSE(database.isConnected());
    database.connect(url);
    EXPECT_EQ(runner.status().currentVersion, 0);
}

TEST(MigrationRunnerTest, BaselineChecksThatHistoryWasActuallyRecorded)
{
    orm::Database database;
    database.connect("sqlite3://:memory:");
    Runner runner{database, initialCatalog()};
    runner.up(10);
    runner.down(0);
    auto& session = orm::migrations::detail::DatabaseAccess::session(database);
    session << "CREATE TRIGGER ignore_baseline BEFORE INSERT ON _orm_migrations BEGIN SELECT RAISE(IGNORE); END;";
    expectMigrationError([&] { runner.baseline(10); }, ErrorCode::InvalidHistory);
    EXPECT_EQ(runner.status().currentVersion, 0);
}

TEST_P(MigrationTest, NativeRuntimeRequiresAnActiveTransaction)
{
    const auto& runtime = orm::migrations::detail::DatabaseAccess::runtime(connection);
    expectMigrationError([&] { runtime.executeMigrationScript(session(), "SELECT 1;"); }, ErrorCode::Execution);
    if (GetParam().type == orm::db::BackendType::Sqlite)
    {
        session().begin();
        EXPECT_THROW(runtime.beginMigration(session(), historyTable()), soci::soci_error);
        session().rollback();
    }
}

TEST(MigrationRunnerTest, SQLiteRebuildPreservesDataForeignKeysIndexesAndTriggers)
{
    orm::Database database;
    database.connect("sqlite3://:memory:");
    Runner runner{
        database,
        Catalog{
            migration(1,
                      "CREATE TABLE parent(id INTEGER PRIMARY KEY); INSERT INTO parent VALUES(1);"
                      "CREATE TABLE child(id INTEGER PRIMARY KEY,parent_id INTEGER REFERENCES parent(id),value TEXT);"
                      "INSERT INTO child VALUES(7,1,'42');"),
            migration(
                2,
                "PRAGMA defer_foreign_keys=ON;"
                "CREATE TABLE child_new(id INTEGER PRIMARY KEY,parent_id INTEGER REFERENCES parent(id),value INTEGER);"
                "INSERT INTO child_new SELECT id,parent_id,CAST(value AS INTEGER) FROM child;"
                "DROP TABLE child; ALTER TABLE child_new RENAME TO child;"
                "CREATE INDEX child_parent ON child(parent_id);"
                "CREATE TRIGGER child_positive BEFORE INSERT ON child WHEN NEW.value<0 "
                "BEGIN SELECT RAISE(ABORT,'negative'); END;")}};
    runner.up();
    auto& session = orm::migrations::detail::DatabaseAccess::session(database);
    int value{};
    session << "SELECT value FROM child WHERE id=7 AND parent_id=1", soci::into(value);
    EXPECT_EQ(value, 42);
    int indexes{};
    session << "SELECT COUNT(*) FROM sqlite_master WHERE type='index' AND name='child_parent'", soci::into(indexes);
    EXPECT_EQ(indexes, 1);
    EXPECT_THROW((session << "INSERT INTO child VALUES(8,999,1)"), soci::soci_error);
    EXPECT_THROW((session << "INSERT INTO child VALUES(8,1,-1)"), soci::soci_error);
    EXPECT_EQ(runner.status().currentVersion, 2);
}

namespace
{
class InstrumentedRuntime final : public orm::db::BackendRuntime
{
public:
    explicit InstrumentedRuntime(const BackendRuntime& runtime) : delegate{runtime} {}
    auto open(soci::session& session, std::string_view url) const -> void override
    {
        delegate.open(session, url);
    }
    auto onConnect(soci::session& session) const -> void override
    {
        delegate.onConnect(session);
    }
    auto tableExists(soci::session& session, std::string_view table) const -> bool override
    {
        return delegate.tableExists(session, table);
    }
    auto limits(soci::session& session) const -> orm::db::BackendRuntimeLimits override
    {
        return delegate.limits(session);
    }
    auto normalizeAffectedRows(long long rows) const -> std::size_t override
    {
        return delegate.normalizeAffectedRows(rows);
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
    auto migrationTable(soci::session& session) const -> std::string override
    {
        if (failTable)
        {
            throw soci::soci_error{"injected inspection failure"};
        }
        return delegate.migrationTable(session);
    }
    auto beginMigration(soci::session& session, std::string_view table) const -> void override
    {
        if (beforeBegin)
        {
            beforeBegin(session);
        }
        delegate.beginMigration(session, table);
    }
    auto executeMigrationScript(soci::session& session, std::string_view sql) const -> void override
    {
        delegate.executeMigrationScript(session, sql);
        if (afterScript)
        {
            afterScript(session);
        }
    }
    auto checkLegacyHooks(soci::session& session) const -> void
    {
        EXPECT_THROW((void)BackendRuntime::migrationTable(session), orm::DatabaseError);
        EXPECT_THROW(BackendRuntime::beginMigration(session, "history"), orm::DatabaseError);
        EXPECT_THROW(BackendRuntime::executeMigrationScript(session, "SELECT 1"), orm::DatabaseError);
    }
    bool failTable = false;
    std::function<void(soci::session&)> beforeBegin, afterScript;

private:
    const BackendRuntime& delegate;
};
class InstrumentedBackend final : public orm::db::BackendProvider
{
private:
    orm::db::sqlite::SqliteBackend delegate;

public:
    InstrumentedBackend() : runtimeValue{delegate.runtime()}, capabilitiesValue{delegate.capabilities()} {}
    auto type() const noexcept -> orm::db::BackendType override
    {
        return backendType;
    }
    auto acceptsConnectionString(std::string_view url) const noexcept -> bool override
    {
        return delegate.acceptsConnectionString(url);
    }
    auto capabilities() const noexcept -> const orm::db::BackendCapabilities& override
    {
        return capabilitiesValue;
    }
    auto dialect() const noexcept -> const orm::db::SqlDialect& override
    {
        return delegate.dialect();
    }
    auto runtime() const noexcept -> const orm::db::BackendRuntime& override
    {
        return runtimeValue;
    }
    auto commandGenerator() const noexcept -> const orm::db::CommandGenerator& override
    {
        return delegate.commandGenerator();
    }
    InstrumentedRuntime runtimeValue;
    orm::db::BackendCapabilities capabilitiesValue;
    orm::db::BackendType backendType = orm::db::BackendType::Sqlite;
};
} // namespace

TEST(MigrationRunnerTest, OptInLegacyHooksAndDriverFailures)
{
    auto provider = std::make_unique<InstrumentedBackend>();
    auto* backend = provider.get();
    orm::db::CommandGeneratorFactory factory{std::vector<std::unique_ptr<orm::db::BackendProvider>>{}};
    factory.registerBackend(std::move(provider));
    orm::Database database{std::move(factory)};
    database.connect("sqlite3://:memory:");
    auto& session = orm::migrations::detail::DatabaseAccess::session(database);
    Runner runner{database, initialCatalog()};
    backend->runtimeValue.checkLegacyHooks(session);
    backend->capabilitiesValue.schema.transactionalMigrations = false;
    expectMigrationError([&] { runner.up(); }, ErrorCode::UnsupportedBackend);
    backend->capabilitiesValue.schema.transactionalMigrations = true;
    backend->runtimeValue.failTable = true;
    expectMigrationError([&] { (void)runner.status(); }, ErrorCode::Execution);
    backend->runtimeValue.failTable = false;
    backend->runtimeValue.beforeBegin = [](soci::session&) { throw soci::soci_error{"injected begin failure"}; };
    expectMigrationError([&] { runner.up(); }, ErrorCode::Execution);
    expectMigrationError([&] { runner.baseline(10); }, ErrorCode::Execution);
    backend->runtimeValue.beforeBegin = {};
    backend->runtimeValue.afterScript = [&](soci::session& active)
    {
        expectMigrationError([&] { (void)runner.status(); }, ErrorCode::ActiveTransaction);
        EXPECT_THROW(database.beginTransaction(), orm::DatabaseError);
        active.close();
        throw soci::soci_error{"injected lost connection"};
    };
    expectMigrationError([&] { runner.up(); }, ErrorCode::Execution);
    EXPECT_FALSE(database.isConnected());
}

TEST(MigrationRunnerTest, HistoryChangedBetweenReadAndWrite)
{
    auto provider = std::make_unique<InstrumentedBackend>();
    auto* backend = provider.get();
    std::vector<std::unique_ptr<orm::db::BackendProvider>> providers;
    providers.push_back(std::move(provider));
    orm::db::CommandGeneratorFactory factory{std::move(providers)};
    orm::Database database{std::move(factory)};
    database.connect("sqlite3://:memory:");
    Runner runner{database, initialCatalog()};
    runner.up(10);
    backend->runtimeValue.beforeBegin = [](soci::session& session)
    { session << "UPDATE _orm_migrations SET name='concurrent_edit'"; };
    expectMigrationError([&] { runner.up(); }, ErrorCode::HistoryChanged);
    backend->runtimeValue.beforeBegin = {};
    orm::migrations::detail::DatabaseAccess::session(database) << "DELETE FROM _orm_migrations";
    backend->runtimeValue.beforeBegin = [](soci::session& session)
    { session << "INSERT INTO _orm_migrations VALUES(10,'concurrent',1,'checksum','now',0)"; };
    expectMigrationError([&] { runner.baseline(10); }, ErrorCode::HistoryChanged);
}

TEST(MigrationCatalogTest, Sha256StandardVectors)
{
    EXPECT_EQ(orm::migrations::detail::sha256(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(orm::migrations::detail::sha256("abc"),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(orm::migrations::detail::sha256(std::string(1000000, 'a')),
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST(MigrationCatalogTest, FilesAndEmbeddedSqlHaveSameNormalizedContents)
{
    TempDirectory directory;
    directory.write("30_second/postgresql/up.sql", "SELECT 2;\r\n");
    directory.write("10_first/sqlite/up.sql", "\xef\xbb\xbfSELECT 1;\r\n");
    directory.write("10_first/sqlite/down.sql", "SELECT 0;\r\n");
    auto catalog = Catalog::fromDirectory(directory.path);
    ASSERT_EQ(catalog.migrations().size(), 2);
    EXPECT_EQ(catalog.migrations()[0].version, 10);
    const auto& loaded = catalog.migrations()[0].scripts.at(orm::db::BackendType::Sqlite);
    SqlMigration embedded{orm::migrations::detail::normalizeSql("SELECT 1;\n"), "SELECT 0;\n"};
    EXPECT_EQ(orm::migrations::detail::checksum(loaded), orm::migrations::detail::checksum(embedded));
    EXPECT_NE(orm::migrations::detail::checksum({"SELECT 1;", std::nullopt}),
              orm::migrations::detail::checksum({"SELECT 1;", ""}));
}

TEST(MigrationCatalogTest, InvalidCatalogsAndPaths)
{
    for (const auto version : {Version{0}, Version{-1}})
    {
        EXPECT_THROW(Catalog{migration(version, "SELECT 1;")}, MigrationError);
    }
    EXPECT_THROW((Catalog{migration(1, "SELECT 1;"), migration(1, "SELECT 2;")}), MigrationError);
    EXPECT_THROW(Catalog{migration(1, "")}, MigrationError);
    EXPECT_THROW(Catalog{migration(1, std::string{"a\0b", 3})}, MigrationError);
    EXPECT_THROW((Catalog{Migration{1, "bad name", {{orm::db::BackendType::Sqlite, {"SELECT 1;", std::nullopt}}}}}),
                 MigrationError);
    EXPECT_THROW((Catalog{Migration{1, "empty", {}}}), MigrationError);
    EXPECT_THROW((Catalog{Migration{1, "unsupported", {{orm::db::BackendType::Empty, {"SELECT 1;", std::nullopt}}}}}),
                 MigrationError);
    TempDirectory directory;
    EXPECT_THROW((void)Catalog::fromDirectory(directory.path / "missing"), MigrationError);
    directory.write("bad/up.sql", "SELECT 1;");
    EXPECT_THROW((void)Catalog::fromDirectory(directory.path), MigrationError);
}

TEST(MigrationCatalogTest, PostgresqlLexerRejectsUnsafeCommandsButAcceptsQuotedBodies)
{
    for (const auto sql :
         {"COMMIT;", "SET search_path=public;", "RESET ALL;", "PREPARE TRANSACTION 'x';", "COPY x FROM STDIN;",
          "CREATE FUNCTION f() RETURNS INT LANGUAGE SQL BEGIN ATOMIC SELECT 1; END;", "SELECT 'unterminated", "/* open",
          "DO $tag$ open", "\\i file", "SET \"CONSTRAINTS\"=1;", "COPY x TO STDOUT;",
          "DROP TABLE \"_orm_migrations\";"})
    {
        EXPECT_THROW(orm::migrations::detail::validateScript(sql, orm::db::BackendType::Postgres, 1), MigrationError);
    }
    EXPECT_NO_THROW(orm::migrations::detail::validateScript(
        "/* nested /* ok */ */ DO $$BEGIN NULL; END;$$; SELECT E'a\\\'b'; SELECT \"a\"\"b\";",
        orm::db::BackendType::Postgres, 1));
    EXPECT_NO_THROW(orm::migrations::detail::validateScript("SELECT $1;", orm::db::BackendType::Postgres, 1));
}

TEST(MigrationCatalogTest, StrictDirectoryLayoutAndUnreadableFiles)
{
    const auto check = [](const std::filesystem::path& relative, bool directory)
    {
        TempDirectory temporary;
        if (directory)
        {
            std::filesystem::create_directories(temporary.path / relative);
        }
        else
        {
            temporary.write(relative, "SELECT 1;");
        }
        expectMigrationError([&] { (void)Catalog::fromDirectory(temporary.path); }, ErrorCode::InvalidCatalog);
    };
    check("10_name/sqlite/mistake.sql", false);
    check("10_name/sqlite/up.sql", true);
    check("x_bad/sqlite/up.sql", false);
    check("10_name/mysql/up.sql", false);
    check("10_name/sqlite/down.sql", false);
    TempDirectory files;
    files.write("1_same/sqlite/up.sql", "SELECT 1;");
    files.write("01_duplicate/sqlite/up.sql", "SELECT 1;");
    expectMigrationError([&] { (void)Catalog::fromDirectory(files.path); }, ErrorCode::InvalidCatalog);
    TempDirectory links;
    std::error_code error;
    std::filesystem::create_directory_symlink(files.path, links.path / "1_link", error);
    if (!error)
    {
        EXPECT_THROW((void)Catalog::fromDirectory(links.path), MigrationError);
        EXPECT_THROW((void)Catalog::fromDirectory(links.path / "1_link"), MigrationError);
    }
    TempDirectory unreadable;
    unreadable.write("1_name/sqlite/up.sql", "SELECT 1;");
    const auto sql = unreadable.path / "1_name/sqlite/up.sql";
    std::filesystem::permissions(sql, std::filesystem::perms::none, error);
    if (!std::ifstream{sql})
    {
        EXPECT_THROW((void)Catalog::fromDirectory(unreadable.path), MigrationError);
    }
    std::filesystem::permissions(sql, std::filesystem::perms::owner_all, error);
}

TEST(MigrationCommandLineTest, ArgumentsConnectionsAndCommands)
{
    const auto call = [](std::initializer_list<std::string_view> args, std::optional<std::string_view> connection)
    {
        std::ostringstream output, errors;
        const auto result = orm::migrations::detail::runCommandLineWithConnection(
            std::span{args.begin(), args.size()}, initialCatalog(), connection, output, errors);
        return std::tuple{result, output.str(), errors.str()};
    };
    EXPECT_EQ(std::get<0>(call({}, std::nullopt)), 0);
    EXPECT_EQ(std::get<0>(call({"--help"}, std::nullopt)), 0);
    EXPECT_EQ(std::get<0>(call({"help"}, std::nullopt)), 0);
    for (const auto& args : {std::vector<std::string_view>{"invalid"},
                             {"preview"},
                             {"preview", "sideways"},
                             {"down"},
                             {"baseline"},
                             {"status", "--to", "1"},
                             {"up", "--to"},
                             {"up", "--to", "bad"},
                             {"up", "--to", "-1"},
                             {"up", "--to", "1x"},
                             {"up", "--other", "1"}})
    {
        std::ostringstream out, err;
        EXPECT_EQ(orm::migrations::detail::runCommandLineWithConnection(args, initialCatalog(), std::nullopt, out, err),
                  2);
    }
    EXPECT_EQ(std::get<0>(call({"status"}, std::nullopt)), 1);
    EXPECT_EQ(std::get<0>(call({"status"}, "")), 1);
    const auto secretFailure = call({"status"}, "invalid://super-secret");
    EXPECT_EQ(std::get<0>(secretFailure), 1);
    EXPECT_EQ(std::get<2>(secretFailure).find("super-secret"), std::string::npos);
    for (const auto& args : {std::vector<std::string_view>{"status"},
                             {"validate"},
                             {"preview", "up"},
                             {"preview", "down"},
                             {"up"},
                             {"down", "--to", "0"},
                             {"baseline", "--to", "10"}})
    {
        std::ostringstream out, err;
        EXPECT_EQ(orm::migrations::detail::runCommandLineWithConnection(args, initialCatalog(),
                                                                        "sqlite3://:memory:", out, err),
                  0)
            << err.str();
    }
    EXPECT_EQ(std::get<0>(call({"up", "--to", "999"}, "sqlite3://:memory:")), 1);
    std::ostringstream out, err;
    const std::string_view help[]{"--help"};
    EXPECT_EQ(runCommandLine(help, initialCatalog(), out, err), 0);
    const char* argv[]{"migrate", "--help"};
    EXPECT_EQ(runCommandLine(2, argv, initialCatalog()), 0);
}

TEST(MigrationCommandLineTest, PersistentHistoryDiagnosticsAndNativeErrors)
{
    TempDirectory directory;
    const auto url = "sqlite3://" + (directory.path / "cli.db").generic_string();
    const auto call = [&](std::initializer_list<std::string_view> args, const Catalog& catalog)
    {
        std::ostringstream output, errors;
        const auto result = orm::migrations::detail::runCommandLineWithConnection(std::span{args.begin(), args.size()},
                                                                                  catalog, url, output, errors);
        return std::tuple{result, output.str(), errors.str()};
    };
    const auto catalog = initialCatalog();
    EXPECT_EQ(std::get<0>(call({"up", "--to", "10"}, catalog)), 0);
    EXPECT_NE(std::get<1>(call({"status"}, catalog)).find("applied"), std::string::npos);
    EXPECT_EQ(std::get<0>(call({"down", "--to", "0"}, catalog)), 0);
    EXPECT_EQ(std::get<0>(call({"baseline", "--to", "10"}, catalog)), 0);
    EXPECT_NE(std::get<1>(call({"status"}, catalog)).find("step_10 baseline"), std::string::npos);
    orm::Database database;
    database.connect(url);
    orm::migrations::detail::DatabaseAccess::session(database) << "UPDATE _orm_migrations SET checksum='changed'";
    database.disconnect();
    const auto diagnostic = call({"status"}, catalog);
    EXPECT_EQ(std::get<0>(diagnostic), 1);
    EXPECT_NE(std::get<2>(diagnostic).find("SQL has changed"), std::string::npos);
    std::ostringstream output, errors;
    const std::string_view up[]{"up"};
    EXPECT_EQ(orm::migrations::detail::runCommandLineWithConnection(up, Catalog{migration(1, "NOT VALID SQL;")},
                                                                    "sqlite3://:memory:", output, errors),
              1);
    EXPECT_NE(errors.str().find("(1)"), std::string::npos);
    EXPECT_EQ(errors.str().find("NOT VALID SQL"), std::string::npos);
}
