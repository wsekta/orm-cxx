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
                     .scripts = {{orm::db::BackendType::Sqlite, scripts},
                                 {orm::db::BackendType::Postgres, scripts},
                                 {orm::db::BackendType::Mysql, scripts},
                                 {orm::db::BackendType::Mariadb, scripts}}};
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
    auto validateMigrationSession(soci::session& session) const -> void override
    {
        if (failValidation)
        {
            throw soci::soci_error{"injected session inspection failure"};
        }
        delegate.validateMigrationSession(session);
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
    auto migrationHistoryExists(soci::session& session, std::string_view table) const -> bool override
    {
        return delegate.migrationHistoryExists(session, table);
    }
    auto createMigrationHistory(soci::session& session, std::string_view table) const -> void override
    {
        delegate.createMigrationHistory(session, table);
    }
    auto executeMigrationScript(soci::session& session, std::string_view sql) const -> void override
    {
        delegate.executeMigrationScript(session, sql);
        if (afterScript)
        {
            afterScript(session);
        }
    }
    auto checkDefaultHooks(soci::session& session) const -> void
    {
        EXPECT_NO_THROW(BackendRuntime::validateMigrationSession(session));
        EXPECT_THROW((void)BackendRuntime::migrationTable(session), orm::DatabaseError);
        EXPECT_THROW(BackendRuntime::beginMigration(session, "history"), orm::DatabaseError);
        EXPECT_THROW(BackendRuntime::executeMigrationScript(session, "SELECT 1"), orm::DatabaseError);
        EXPECT_THROW((void)BackendRuntime::migrationHistoryExists(session, "history"), orm::DatabaseError);
        EXPECT_THROW(BackendRuntime::createMigrationHistory(session, "history"), orm::DatabaseError);
        EXPECT_TRUE(BackendRuntime::migrationJournalTable(session, "history").empty());
        EXPECT_NO_THROW(BackendRuntime::endMigration(session, "history"));
    }
    bool failTable = false, failValidation = false;
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

TEST(MigrationRunnerTest, OptInHooksAndDriverFailures)
{
    auto provider = std::make_unique<InstrumentedBackend>();
    auto* backend = provider.get();
    orm::db::CommandGeneratorFactory factory{std::vector<std::unique_ptr<orm::db::BackendProvider>>{}};
    factory.registerBackend(std::move(provider));
    orm::Database database{std::move(factory)};
    database.connect("sqlite3://:memory:");
    auto& session = orm::migrations::detail::DatabaseAccess::session(database);
    Runner runner{database, initialCatalog()};
    backend->runtimeValue.checkDefaultHooks(session);
    backend->capabilitiesValue.migrations.supported = false;
    expectMigrationError([&] { runner.up(); }, ErrorCode::UnsupportedBackend);
    backend->capabilitiesValue.migrations.supported = true;
    backend->runtimeValue.failValidation = true;
    expectMigrationError([&] { (void)runner.status(); }, ErrorCode::Execution);
    backend->runtimeValue.failValidation = false;
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
