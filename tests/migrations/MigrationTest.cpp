module;

#include <filesystem>
#include <fstream>
#include <locale>
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
    SqlMigration scripts;
    scripts.up = std::move(up);
    scripts.down = std::move(down);
    return Migration{.version = version,
                     .name = "step_" + std::to_string(version),
                     .scripts = {{orm::db::BackendType::Sqlite, scripts},
                                 {orm::db::BackendType::Postgres, scripts},
                                 {orm::db::BackendType::Mysql, scripts},
                                 {orm::db::BackendType::Mariadb, scripts}},
                     .before = {},
                     .after = {}};
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
class FailingReadCodecvt final : public std::codecvt<char, char, std::mbstate_t>
{
protected:
    auto do_always_noconv() const noexcept -> bool override
    {
        return false;
    }
    auto do_in(std::mbstate_t&, const char* first, const char*, const char*& next, char* destination, char*,
               char*& destinationNext) const -> result override
    {
        next = first;
        destinationNext = destination;
        return error;
    }
};
class ScopedReadFailureLocale
{
public:
    ScopedReadFailureLocale()
    {
        std::locale::global(std::locale{original_, new FailingReadCodecvt});
    }
    ~ScopedReadFailureLocale()
    {
        std::locale::global(original_);
    }

private:
    std::locale original_;
};
} // namespace

class MigrationTest : public DatabaseTest<models::Schema>
{
public:
    auto nontransactional() const -> bool
    {
        return connection.getBackendCapabilities().migrations.atomicity ==
               orm::db::MigrationAtomicity::NonTransactional;
    }
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
    EXPECT_EQ(plan.atomicity, connection.getBackendCapabilities().migrations.atomicity);
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
    if (nontransactional())
    {
        GTEST_SKIP() << "Covered by nontransactional recovery tests";
    }
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
    if (nontransactional())
    {
        GTEST_SKIP() << "Covered by nontransactional recovery tests";
    }
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
    Runner runner{connection,
                  Catalog{Migration{1, "only_other", {{other, {"SELECT 1;", std::nullopt, {}, {}, {}, {}}}}, {}, {}}}};
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
    else if (GetParam().type == orm::db::BackendType::Postgres)
    {
        sql += "CREATE FUNCTION migration_function() RETURNS integer LANGUAGE plpgsql AS $body$ BEGIN RETURN 42; END; "
               "$body$; DO $$ BEGIN INSERT INTO migration_users VALUES(2,'function;done'); END; $$;";
    }
    else
    {
        sql += "CREATE PROCEDURE migration_procedure() BEGIN INSERT INTO migration_users VALUES(2,'procedure;done'); "
               "SELECT 'drain;result'; END; CALL migration_procedure(); "
               "CREATE TRIGGER migration_trigger BEFORE INSERT ON migration_users FOR EACH ROW "
               "BEGIN SET NEW.name='trigger;done'; END;";
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
    runtime.endMigration(session(), table);
    EXPECT_NO_THROW(second.up());
}

INSTANTIATE_TEST_SUITE_P(DatabaseTest, MigrationTest, conformanceBackendTestConfigs, backendTestName);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(MigrationTest);

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
    if (nontransactional())
    {
        GTEST_SKIP() << "Covered by nontransactional recovery tests";
    }
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

TEST_P(MigrationTest, NativeRuntimeRequiresAnActiveTransaction)
{
    if (nontransactional())
    {
        GTEST_SKIP() << "Nontransactional scripts require a session lock";
    }
    const auto& runtime = orm::migrations::detail::DatabaseAccess::runtime(connection);
    expectMigrationError([&] { runtime.executeMigrationScript(session(), "SELECT 1;"); }, ErrorCode::Execution);
    if (GetParam().type == orm::db::BackendType::Sqlite)
    {
        session().begin();
        EXPECT_THROW(runtime.beginMigration(session(), historyTable()), soci::soci_error);
        session().rollback();
    }
}

TEST_P(MigrationTest, NontransactionalUpRequiresRepairAndExplicitResolution)
{
    if (!nontransactional())
    {
        GTEST_SKIP();
    }
    const auto catalog = Catalog{
        migration(1,
                  "CREATE TABLE migration_users(id INTEGER PRIMARY KEY); INSERT INTO migration_users VALUES(1); "
                  "INSERT INTO missing VALUES(1);",
                  "DROP TABLE migration_users;")};
    Runner runner{connection, catalog};
    expectMigrationError([&] { runner.up(); }, ErrorCode::Execution);
    auto interrupted = runner.status();
    ASSERT_TRUE(interrupted.incomplete);
    EXPECT_FALSE(interrupted.isValid());
    EXPECT_EQ(interrupted.currentVersion, 0);
    EXPECT_EQ(interrupted.incomplete->direction, Direction::Up);
    EXPECT_EQ(users(), 1); // The successful DDL and DML survived the failing statement.
    const auto checksum = interrupted.incomplete->checksum;
    expectMigrationError([&] { runner.up(); }, ErrorCode::IncompleteMigration);
    expectMigrationError([&] { runner.down(0); }, ErrorCode::IncompleteMigration);
    expectMigrationError([&] { runner.baseline(1); }, ErrorCode::IncompleteMigration);
    expectMigrationError([&] { runner.validate(); }, ErrorCode::IncompleteMigration);
    expectMigrationError([&] { (void)runner.preview(Direction::Up); }, ErrorCode::IncompleteMigration);
    expectMigrationError([&] { runner.resolve(2, checksum, Resolution::Reverted); }, ErrorCode::InvalidResolution);
    expectMigrationError([&] { runner.resolve(1, "incorrect", Resolution::Completed); }, ErrorCode::InvalidResolution);
    // Deliberately exercise rejection of a value outside the named resolution choices.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    expectMigrationError([&] { runner.resolve(1, checksum, static_cast<Resolution>(99)); },
                         ErrorCode::InvalidResolution);
    Runner edited{connection, Catalog{migration(1, "SELECT 1;")}};
    expectMigrationError([&] { edited.resolve(1, checksum, Resolution::Completed); }, ErrorCode::ChecksumMismatch);

    connection.disconnect();
    connection.connect(GetParam().type, testConnectionString());
    EXPECT_EQ(runner.status().incomplete, interrupted.incomplete);
    session() << "DROP TABLE migration_users";
    runner.resolve(1, checksum, Resolution::Reverted);
    EXPECT_TRUE(runner.status().isValid());
    EXPECT_EQ(runner.status().currentVersion, 0);
    expectMigrationError([&] { runner.resolve(1, checksum, Resolution::Reverted); }, ErrorCode::InvalidResolution);

    expectMigrationError([&] { runner.up(); }, ErrorCode::Execution);
    session() << "CREATE TABLE missing(id INTEGER)";
    session() << "INSERT INTO missing VALUES(1)";
    runner.resolve(1, checksum, Resolution::Completed);
    EXPECT_EQ(runner.status().currentVersion, 1);
    EXPECT_FALSE(runner.status().incomplete);
    runner.down(0);
    EXPECT_EQ(runner.status().currentVersion, 0);
}

TEST_P(MigrationTest, NontransactionalDownCanBeCompletedOrReverted)
{
    if (!nontransactional())
    {
        GTEST_SKIP();
    }
    Runner runner{
        connection,
        Catalog{migration(1, "CREATE TABLE migration_users(id INTEGER PRIMARY KEY);", "DROP TABLE migration_users;"),
                migration(2, "ALTER TABLE migration_users ADD COLUMN email TEXT;",
                          "ALTER TABLE migration_users DROP COLUMN email; SELECT * FROM missing;")}};
    runner.up();
    expectMigrationError([&] { runner.down(1); }, ErrorCode::Execution);
    const auto incomplete = runner.status().incomplete;
    ASSERT_TRUE(incomplete);
    EXPECT_EQ(incomplete->direction, Direction::Down);
    EXPECT_EQ(runner.status().currentVersion, 2);
    EXPECT_THROW(session() << "SELECT email FROM migration_users", soci::soci_error);
    session() << "ALTER TABLE migration_users ADD COLUMN email TEXT";
    runner.resolve(2, incomplete->checksum, Resolution::Reverted);
    EXPECT_EQ(runner.status().currentVersion, 2);
    expectMigrationError([&] { runner.down(1); }, ErrorCode::Execution);
    runner.resolve(2, incomplete->checksum, Resolution::Completed);
    EXPECT_EQ(runner.status().currentVersion, 1);
    runner.up();
    EXPECT_EQ(runner.status().currentVersion, 2);
}

TEST_P(MigrationTest, DurableMarkerSurvivesLossBeforeHistoryFinalization)
{
    if (!nontransactional())
    {
        GTEST_SKIP();
    }
    const auto catalog = Catalog{migration(1, "CREATE TABLE migration_users(id INTEGER PRIMARY KEY);")};
    const auto& entry = catalog.migrations().front();
    const auto checksum = orm::migrations::detail::checksum(entry.scripts.at(GetParam().type));
    auto& runtime = orm::migrations::detail::DatabaseAccess::runtime(connection);
    const auto table = runtime.migrationTable(session());
    runtime.beginMigration(session(), table);
    runtime.createMigrationHistory(session(), table);
    const auto journal = runtime.migrationJournalTable(session(), table);
    const auto backend = static_cast<int>(GetParam().type);
    session() << "INSERT INTO " + journal +
                     "(slot,version,name,backend,direction,checksum) VALUES(1,1,'step_1',:backend,0,:checksum)",
        soci::use(backend, "backend"), soci::use(checksum, "checksum");
    runtime.executeMigrationScript(session(), entry.scripts.at(GetParam().type).up);
    connection.disconnect(); // Lock released; journal and application DDL remain durable.
    connection.connect(GetParam().type, testConnectionString());
    Runner restarted{connection, catalog};
    ASSERT_TRUE(restarted.status().incomplete);
    EXPECT_EQ(restarted.status().currentVersion, 0);
    expectMigrationError([&] { restarted.up(); }, ErrorCode::IncompleteMigration);
    restarted.resolve(1, checksum, Resolution::Completed);
    EXPECT_EQ(restarted.status().currentVersion, 1);
    EXPECT_EQ(users(), 0);
}

TEST_P(MigrationTest, ResolveIsUnavailableWithoutAnIncompleteMigration)
{
    Runner runner{connection, initialCatalog()};
    expectMigrationError([&] { runner.resolve(10, std::string(64, 'a'), Resolution::Completed); },
                         ErrorCode::InvalidResolution);
}

TEST_P(MigrationTest, CommandLineReportsAndResolvesAnIncompleteMigration)
{
    if (!nontransactional())
    {
        GTEST_SKIP();
    }
    const auto catalog =
        Catalog{migration(1, "CREATE TABLE migration_users(id INTEGER PRIMARY KEY); SELECT * FROM missing;",
                          "DROP TABLE migration_users; SELECT * FROM missing;")};
    Runner runner{connection, catalog};
    expectMigrationError([&] { runner.up(); }, ErrorCode::Execution);
    const auto checksum = runner.status().incomplete->checksum;
    const auto call = [&](std::initializer_list<std::string_view> args)
    {
        std::ostringstream output, errors;
        const auto result = orm::migrations::detail::runCatalogCommandWithConnection(
            std::span{args.begin(), args.size()}, catalog, testConnectionString(), output, errors);
        return std::tuple{result, output.str(), errors.str()};
    };
    const auto status = call({"status"});
    EXPECT_EQ(std::get<0>(status), 1);
    EXPECT_NE(std::get<1>(status).find("Incomplete: 1 step_1 up checksum " + checksum), std::string::npos);
    EXPECT_NE(std::get<2>(status).find("Inspect and repair"), std::string::npos);
    EXPECT_EQ(std::get<0>(call({"preview", "up"})), 1);
    session() << "CREATE TABLE missing(id INTEGER)";
    EXPECT_EQ(std::get<0>(call({"resolve", "--version", "1", "--checksum", checksum, "--resolution", "completed"})), 0);
    EXPECT_EQ(runner.status().currentVersion, 1);
    session() << "DROP TABLE missing";
    expectMigrationError([&] { runner.down(0); }, ErrorCode::Execution);
    session() << "CREATE TABLE migration_users(id INTEGER PRIMARY KEY)";
    EXPECT_EQ(std::get<0>(call({"resolve", "--version", "1", "--checksum", checksum, "--resolution", "reverted"})), 0);
    EXPECT_EQ(runner.status().currentVersion, 1);
}

TEST_P(MigrationTest, InvalidDurableMarkersBlockAllChanges)
{
    if (!nontransactional())
    {
        GTEST_SKIP();
    }
    const auto catalog = Catalog{migration(1, "SELECT 1;")};
    Runner runner{connection, catalog};
    const auto checksum = orm::migrations::detail::checksum(catalog.migrations().front().scripts.at(GetParam().type));
    auto& runtime = orm::migrations::detail::DatabaseAccess::runtime(connection);
    const auto table = runtime.migrationTable(session());
    const auto journal = runtime.migrationJournalTable(session(), table);
    session() << "CREATE TABLE " + journal +
                     " (slot INT,version BIGINT,name TEXT,backend INT,direction INT,checksum TEXT)";
    const auto backend = static_cast<int>(GetParam().type);
    session() << "INSERT INTO " + journal + " VALUES(1,1,'step_1',:backend,0,:checksum)", soci::use(backend, "backend"),
        soci::use(checksum, "checksum");
    session() << "UPDATE " + journal + " SET slot=0";
    expectMigrationError([&] { (void)runner.status(); }, ErrorCode::InvalidHistory);
    session() << "UPDATE " + journal + " SET slot=1,direction=2";
    expectMigrationError([&] { (void)runner.status(); }, ErrorCode::InvalidHistory);
    session() << "UPDATE " + journal + " SET direction=0,backend=-1";
    expectMigrationError([&] { (void)runner.status(); }, ErrorCode::InvalidHistory);
    session() << "UPDATE " + journal + " SET backend=:backend,name='different'", soci::use(backend, "backend");
    expectMigrationError([&] { runner.up(); }, ErrorCode::InvalidHistory);
    session() << "UPDATE " + journal + " SET name='step_1',checksum='changed'";
    expectMigrationError([&] { runner.up(); }, ErrorCode::ChecksumMismatch);
    session() << "UPDATE " + journal + " SET checksum=:checksum", soci::use(checksum, "checksum");
    session() << "INSERT INTO " + journal + " SELECT 2,version,name,backend,direction,checksum FROM " + journal;
    expectMigrationError([&] { (void)runner.status(); }, ErrorCode::InvalidHistory);
    session() << "DROP TABLE " + journal;
    session() << "CREATE TABLE " + journal +
                     " (slot TEXT,version BIGINT,name TEXT,backend INT,direction INT,checksum TEXT)";
    session() << "INSERT INTO " + journal + " VALUES('wrong',1,'step_1',:backend,0,:checksum)",
        soci::use(backend, "backend"), soci::use(checksum, "checksum");
    expectMigrationError([&] { (void)runner.status(); }, ErrorCode::InvalidHistory);
}

TEST_P(MigrationTest, MysqlFamilyRejectsUnsafeSessionModesBeforeApplicationSql)
{
    if (!nontransactional())
    {
        GTEST_SKIP();
    }
    std::string originalMode;
    session() << "SELECT @@SESSION.sql_mode", soci::into(originalMode);
    Runner runner{connection, Catalog{migration(1, "CREATE TABLE migration_users(id INTEGER PRIMARY KEY);")}};
    for (const auto mode : {"ANSI_QUOTES", "NO_BACKSLASH_ESCAPES", ""})
    {
        const auto altered =
            originalMode.empty() || std::string_view{mode}.empty() ? std::string{mode} : originalMode + "," + mode;
        session() << "SET SESSION sql_mode='" + altered + "'";
        expectMigrationError([&] { runner.up(); }, ErrorCode::InvalidSql);
        expectMigrationError([&] { (void)runner.status(); }, ErrorCode::InvalidSql);
        session() << "SET SESSION sql_mode='" + originalMode + "'";
        EXPECT_FALSE(runner.status().incomplete);
        EXPECT_FALSE(
            orm::migrations::detail::DatabaseAccess::runtime(connection).tableExists(session(), "migration_users"));
    }
    if (GetParam().type == orm::db::BackendType::Mariadb)
    {
        session() << "SET SESSION max_sort_length=32";
        expectMigrationError([&] { runner.up(); }, ErrorCode::InvalidSql);
        session() << "SET SESSION max_sort_length=65536";
        EXPECT_FALSE(runner.status().incomplete);
    }
    runner.up();
    EXPECT_EQ(runner.status().currentVersion, 1);
}

TEST_P(MigrationTest, MysqlFamilyDetectsSettingsChangesThroughStoredRoutine)
{
    if (!nontransactional())
    {
        GTEST_SKIP();
    }
    int originalSortLength{};
    session() << "SELECT @@SESSION.max_sort_length", soci::into(originalSortLength);
    session() << "CREATE PROCEDURE migration_change_settings() SET SESSION max_sort_length=" +
                     std::to_string(originalSortLength == 32 ? 64 : 32);
    Runner runner{connection, Catalog{migration(1, "CALL migration_change_settings(); "
                                                   "CREATE TABLE migration_users(id INTEGER PRIMARY KEY);")}};
    expectMigrationError([&] { runner.up(); }, ErrorCode::InvalidSql);
    EXPECT_FALSE(connection.isConnected());
    connection.connect(GetParam().type, testConnectionString());
    const auto state = runner.status();
    ASSERT_TRUE(state.incomplete);
    EXPECT_EQ(state.currentVersion, 0);
    EXPECT_TRUE(orm::migrations::detail::DatabaseAccess::runtime(connection).tableExists(session(), "migration_users"));
    runner.resolve(1, state.incomplete->checksum, Resolution::Completed);
    EXPECT_TRUE(runner.status().isValid());
    EXPECT_EQ(runner.status().currentVersion, 1);
}

TEST_P(MigrationTest, MysqlFamilyHistoryFinalizationFailurePreservesDurableMarker)
{
    if (!nontransactional())
    {
        GTEST_SKIP();
    }
    const auto& runtime = orm::migrations::detail::DatabaseAccess::runtime(connection);
    const auto history = historyTable();
    const auto journal = runtime.migrationJournalTable(session(), history);
    Runner runner{connection, Catalog{migration(1, "SELECT 1;"),
                                      migration(2, "CREATE TABLE migration_users(id INTEGER PRIMARY KEY);"),
                                      migration(3, "INSERT INTO migration_users VALUES(1);")}};
    runner.baseline(1);
    for (const auto version : {Version{2}, Version{3}})
    {
        session() << "CREATE TRIGGER migration_fail_finalization BEFORE " +
                         std::string{version == 2 ? "INSERT ON " : "DELETE ON "} + (version == 2 ? history : journal) +
                         " FOR EACH ROW SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='fault'";
        expectMigrationError([&] { runner.up(version); }, ErrorCode::Execution);
        const auto state = runner.status();
        ASSERT_TRUE(state.incomplete);
        EXPECT_EQ(state.currentVersion, version - 1);
        EXPECT_EQ(state.incomplete->version, version);
        EXPECT_TRUE(runtime.tableExists(session(), "migration_users"));
        if (version == 3)
        {
            EXPECT_EQ(users(), 1);
        }
        expectMigrationError([&] { runner.resolve(version, state.incomplete->checksum, Resolution::Completed); },
                             ErrorCode::Execution);
        EXPECT_EQ(runner.status().currentVersion, version - 1);
        EXPECT_EQ(runner.status().incomplete, state.incomplete);
        session() << "DROP TRIGGER migration_fail_finalization";
        runner.resolve(version, state.incomplete->checksum, Resolution::Completed);
        EXPECT_FALSE(runner.status().incomplete);
        EXPECT_EQ(runner.status().currentVersion, version);
    }
}

TEST_P(MigrationTest, MysqlFamilyRejectsNontransactionalAndViewLedgers)
{
    if (!nontransactional())
    {
        GTEST_SKIP();
    }
    Runner runner{connection, Catalog{migration(1, "CREATE TABLE migration_users(id INTEGER PRIMARY KEY);")}};
    const auto& runtime = orm::migrations::detail::DatabaseAccess::runtime(connection);
    const auto history = runtime.migrationTable(session());
    const auto journal = runtime.migrationJournalTable(session(), history);
    for (const auto& table : {history, journal})
    {
        session() << "CREATE TABLE " + table + " (dummy INT) ENGINE=MyISAM";
        expectMigrationError([&] { runner.up(); }, ErrorCode::InvalidHistory);
        EXPECT_FALSE(runtime.tableExists(session(), "migration_users"));
        session() << "DROP TABLE " + table;
        session() << "CREATE VIEW " + table + " AS SELECT 1 AS dummy";
        expectMigrationError([&] { runner.baseline(1); }, ErrorCode::InvalidHistory);
        EXPECT_FALSE(runtime.tableExists(session(), "migration_users"));
        session() << "DROP VIEW " + table;
    }
    runner.up();
    EXPECT_EQ(runner.status().currentVersion, 1);
}

TEST_P(MigrationTest, IncompleteDowngradeCannotRemoveABaseline)
{
    if (!nontransactional())
    {
        GTEST_SKIP();
    }
    const auto catalog = Catalog{migration(1, "SELECT 1;", "")};
    Runner runner{connection, catalog};
    runner.baseline(1);
    const auto& runtime = orm::migrations::detail::DatabaseAccess::runtime(connection);
    const auto journal = runtime.migrationJournalTable(session(), historyTable());
    const auto backend = static_cast<int>(GetParam().type);
    const auto checksum = orm::migrations::detail::checksum(catalog.migrations().front().scripts.at(GetParam().type));
    session() << "INSERT INTO " + journal + " VALUES(1,1,'step_1',:backend,1,:checksum)", soci::use(backend, "backend"),
        soci::use(checksum, "checksum");
    expectMigrationError([&] { runner.resolve(1, checksum, Resolution::Completed); }, ErrorCode::InvalidHistory);
    EXPECT_EQ(runner.status().baselineVersion, 1);
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
    constexpr std::array backends{orm::db::BackendType::Sqlite};
    const orm::model::SchemaView models{};
    auto project = MigrationProject::init(directory.path, backends);
    project.makeManual("first", models);
    directory.write("1_first/sqlite/up.sql", "\xef\xbb\xbfSELECT 1;\r\n");
    directory.write("1_first/sqlite/down.sql", "SELECT 0;\r\n");
    project.finalize(1, models, true);
    project.makeManual("second", models);
    directory.write("2_second/sqlite/up.sql", "SELECT 2;\r\n");
    project.finalize(2, models, true);
    auto catalog = Catalog::fromDirectory(directory.path);
    ASSERT_EQ(catalog.migrations().size(), 2);
    EXPECT_EQ(catalog.migrations()[0].version, 1);
    const auto& loaded = catalog.migrations()[0].scripts.at(orm::db::BackendType::Sqlite);
    auto embedded = loaded;
    embedded.up = "SELECT 1;\n";
    embedded.down = "SELECT 0;\n";
    EXPECT_EQ(orm::migrations::detail::checksum(loaded), orm::migrations::detail::checksum(embedded));
    embedded.down.reset();
    const auto absent = orm::migrations::detail::checksum(embedded);
    embedded.down = "";
    EXPECT_NE(absent, orm::migrations::detail::checksum(embedded));
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
    EXPECT_THROW(
        (Catalog{Migration{
            1, "bad name", {{orm::db::BackendType::Sqlite, {"SELECT 1;", std::nullopt, {}, {}, {}, {}}}}, {}, {}}}),
        MigrationError);
    EXPECT_THROW((Catalog{Migration{1, "empty", {}, {}, {}}}), MigrationError);
    EXPECT_THROW(
        (Catalog{Migration{
            1, "unsupported", {{orm::db::BackendType::Empty, {"SELECT 1;", std::nullopt, {}, {}, {}, {}}}}, {}, {}}}),
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

TEST(MigrationCatalogTest, MysqlAndMariaDbVariantsAreIndependent)
{
    TempDirectory directory;
    constexpr std::array backends{orm::db::BackendType::Mysql, orm::db::BackendType::Mariadb};
    const orm::model::SchemaView models{};
    auto project = MigrationProject::init(directory.path, backends);
    project.makeManual("create", models);
    directory.write("1_create/mysql/up.sql", "SELECT 1;\r\n");
    directory.write("1_create/mariadb/up.sql", "SELECT 2;\n");
    project.finalize(1, models, true);
    const auto catalog = Catalog::fromDirectory(directory.path);
    const auto migrations = catalog.migrations();
    ASSERT_EQ(migrations.size(), 1);
    const auto& variants = migrations.front().scripts;
    EXPECT_EQ(variants.at(orm::db::BackendType::Mysql).up, "SELECT 1;\n");
    EXPECT_EQ(variants.at(orm::db::BackendType::Mariadb).up, "SELECT 2;\n");
    EXPECT_NE(orm::migrations::detail::checksum(variants.at(orm::db::BackendType::Mysql)),
              orm::migrations::detail::checksum(variants.at(orm::db::BackendType::Mariadb)));
}

TEST(MigrationCatalogTest, MysqlFamilyLexerChecksExecutableCommentsAndRoutines)
{
    for (const auto backend : {orm::db::BackendType::Mysql, orm::db::BackendType::Mariadb})
    {
        for (const auto sql : {"COMMIT;",
                               "START TRANSACTION;",
                               "BEGIN; SELECT 1; END;",
                               "USE other;",
                               "DROP DATABASE current_database;",
                               "DROP SCHEMA IF EXISTS current_database;",
                               "SET sql_mode='';",
                               "/*!80000 SET autocommit=0 */;",
                               "/*M!100100 COMMIT */;",
                               "/*! SELECT /* ordinary comment */ GET_LOCK('x',0) */;",
                               "/*M! SELECT /* ordinary comment */ RELEASE_ALL_LOCKS() */;",
                               "#\nCOMMIT;",
                               "# comment\rCOMMIT;",
                               "-- comment\rROLLBACK;",
                               "DELIMITER $$",
                               "SELECT @@autocommit;",
                               "DROP TABLE `_orm_migrations`;",
                               "DELETE FROM _orm_migrations_incomplete;",
                               "CREATE PROCEDURE p() BEGIN SET sql_mode=''; END;",
                               "CREATE PROCEDURE p() BEGIN COMMIT; END;",
                               "CREATE PROCEDURE p() BEGIN DROP DATABASE current_database; END;",
                               "CREATE PROCEDURE p() BEGIN PREPARE x FROM 'COMMIT'; END;",
                               "SELECT RELEASE_LOCK('x');",
                               "SELECT `GET_LOCK`('x',0);",
                               "SELECT `RELEASE_LOCK`('x');",
                               "SELECT `release_all_locks`();",
                               "CREATE PROCEDURE p() BEGIN SELECT 1; END; BEGIN; SELECT 1;",
                               "/* unclosed",
                               "SELECT 'unclosed"})
        {
            EXPECT_THROW(orm::migrations::detail::validateScript(sql, backend, 1), MigrationError) << sql;
        }
        EXPECT_NO_THROW(orm::migrations::detail::validateScript(
            "# COMMIT\n-- ROLLBACK\n/* SET sql_mode='' */ CREATE TABLE `a:b`(id INT); "
            "/*!80000 INSERT INTO `a:b` VALUES(1) */; /*M!100100 SELECT 'native;sql' */; "
            "CREATE PROCEDURE p() BEGIN BEGIN UPDATE `a:b` SET id=2; END; SELECT 'it\\'s;ok'; END; CALL p(); "
            "CREATE TRIGGER t BEFORE INSERT ON `a:b` FOR EACH ROW BEGIN SET NEW.id=3; END;",
            backend, 1));
        EXPECT_NO_THROW(orm::migrations::detail::validateScript(
            "SELECT '/*! COMMIT */'; SELECT \"/*M! COMMIT */\"; SELECT `a``b`;", backend, 1));
        EXPECT_NO_THROW(
            orm::migrations::detail::validateScript("/*! SELECT /* ordinary comment */ 'literal */ text' */; "
                                                    "/*M! SELECT /*! COMMIT */ 1 */;",
                                                    backend, 1));
        EXPECT_NO_THROW(orm::migrations::detail::validateScript(
            "CREATE PROCEDURE p() BEGIN SELECT CASE WHEN 1 THEN 'ok' ELSE 'bad' END; "
            "CASE 1 WHEN 1 THEN BEGIN SELECT 2; END; END CASE; END;",
            backend, 1));
    }
}

TEST(MigrationCatalogTest, ExecutableCommentDelimitersRespectEscapesQuotesAndInnerComments)
{
    for (const auto backend : {orm::db::BackendType::Mysql, orm::db::BackendType::Mariadb})
    {
        for (const auto sql : {"/*! SELECT 'it\\'s */ still a literal', \"double\"\"quote\", `tick``quote` */;",
                               "/*M! SELECT 1 # a line comment containing */\n */;",
                               "/*! SELECT 1 -- a line comment containing */\r\n */;"})
        {
            EXPECT_NO_THROW(orm::migrations::detail::validateScript(sql, backend, 1)) << sql;
        }
        for (const auto sql : {"/*! SELECT 1", "/*M! SELECT 'unterminated */", "/*! SELECT /* unterminated",
                               "/*! SELECT 1 # unterminated */", "/*M! SELECT 1 -- unterminated */"})
        {
            expectMigrationError([&] { orm::migrations::detail::validateScript(sql, backend, 1); },
                                 ErrorCode::InvalidSql);
        }
    }
}

TEST(MigrationCatalogTest, ReportsStreamReadFailureAsInvalidCatalog)
{
    TempDirectory temporary;
    constexpr std::array backends{orm::db::BackendType::Sqlite};
    (void)MigrationProject::init(temporary.path, backends);
    // File permissions do not induce a read failure when tests run as root.
    // A scoped file-stream conversion error gives a deterministic I/O failure.
    ScopedReadFailureLocale locale;
    expectMigrationError([&] { (void)Catalog::fromDirectory(temporary.path); }, ErrorCode::InvalidCatalog);
}

TEST(MigrationCatalogTest, StrictDirectoryLayoutAndUnreadableFiles)
{
    const auto check = [](const std::filesystem::path& relative, bool directory)
    {
        TempDirectory temporary;
        constexpr std::array backends{orm::db::BackendType::Sqlite};
        auto project = MigrationProject::init(temporary.path, backends);
        project.makeManual("name", orm::model::SchemaView{});
        temporary.write("1_name/sqlite/up.sql", "SELECT 1;");
        project.finalize(1, orm::model::SchemaView{}, true);
        if (directory)
        {
            std::filesystem::remove(temporary.path / relative);
            std::filesystem::create_directories(temporary.path / relative);
        }
        else
        {
            temporary.write(relative, "SELECT 1;");
        }
        expectMigrationError([&] { (void)Catalog::fromDirectory(temporary.path); }, ErrorCode::InvalidCatalog);
    };
    check("1_name/sqlite/mistake.sql", false);
    check("1_name/sqlite/up.sql", true);
    check("x_bad/sqlite/up.sql", false);
    check("1_name/unknown/up.sql", false);
    check("1_name/sqlite/down.sql", false);
    TempDirectory files;
    constexpr std::array backends{orm::db::BackendType::Sqlite};
    auto fileProject = MigrationProject::init(files.path, backends);
    fileProject.makeManual("same", orm::model::SchemaView{});
    files.write("1_same/sqlite/up.sql", "SELECT 1;");
    fileProject.finalize(1, orm::model::SchemaView{}, true);
    std::filesystem::copy(files.path / "1_same", files.path / "01_same", std::filesystem::copy_options::recursive);
    expectMigrationError([&] { (void)Catalog::fromDirectory(files.path); }, ErrorCode::InvalidCatalog);
    TempDirectory links;
    (void)MigrationProject::init(links.path, backends);
    std::error_code error;
    std::filesystem::create_directory_symlink(files.path, links.path / "1_link", error);
    if (!error)
    {
        EXPECT_THROW((void)Catalog::fromDirectory(links.path), MigrationError);
        EXPECT_THROW((void)Catalog::fromDirectory(links.path / "1_link"), MigrationError);
    }
    TempDirectory unreadable;
    auto unreadableProject = MigrationProject::init(unreadable.path, backends);
    unreadableProject.makeManual("name", orm::model::SchemaView{});
    unreadable.write("1_name/sqlite/up.sql", "SELECT 1;");
    unreadableProject.finalize(1, orm::model::SchemaView{}, true);
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
        const auto result = orm::migrations::detail::runCatalogCommandWithConnection(
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
                             {"up", "--other", "1"},
                             {"resolve"},
                             {"resolve", "--version", "0", "--checksum", "bad", "--resolution", "completed"},
                             {"resolve", "--version", "1", "--checksum", "bad", "--resolution", "reverted"},
                             {"resolve", "--version", "1", "--checksum", "bad", "--resolution", "invalid"}})
    {
        std::ostringstream out, err;
        EXPECT_EQ(
            orm::migrations::detail::runCatalogCommandWithConnection(args, initialCatalog(), std::nullopt, out, err),
            2);
    }
    EXPECT_EQ(std::get<0>(call({"status"}, std::nullopt)), 1);
    EXPECT_EQ(std::get<0>(call({"status"}, "")), 1);
    const auto secretFailure = call({"status"}, "invalid://super-secret");
    EXPECT_EQ(std::get<0>(secretFailure), 1);
    EXPECT_EQ(std::get<2>(secretFailure).find("super-secret"), std::string::npos);
    if constexpr (orm::config::sqliteBackendEnabled)
    {
        for (const auto& args : {std::vector<std::string_view>{"status"},
                                 {"validate"},
                                 {"preview", "up"},
                                 {"preview", "down"},
                                 {"up"},
                                 {"down", "--to", "0"},
                                 {"baseline", "--to", "10"}})
        {
            std::ostringstream out, err;
            EXPECT_EQ(orm::migrations::detail::runCatalogCommandWithConnection(args, initialCatalog(),
                                                                               "sqlite3://:memory:", out, err),
                      0)
                << err.str();
        }
        EXPECT_EQ(std::get<0>(call({"up", "--to", "999"}, "sqlite3://:memory:")), 1);
    }
    std::ostringstream out, err;
    const std::string_view help[]{"--help"};
    EXPECT_EQ(runCommandLine(help, orm::model::SchemaView{}, "migrations", out, err), 0);
    const char* argv[]{"migrate", "--help"};
    EXPECT_EQ(runCommandLine(2, argv, orm::model::SchemaView{}), 0);
}
