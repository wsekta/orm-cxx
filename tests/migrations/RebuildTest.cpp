module;

#include <filesystem>
#include <limits>
#include <string>

#include "tests/UnitTestPrelude.hpp"

module orm;

import :internal;
import :migrations_internal;

namespace
{
using namespace orm::migrations;
using Backend = orm::db::BackendType;
using Access = detail::DatabaseAccess;

auto sourceSnapshot() -> SchemaSnapshot
{
    auto snapshot = emptySnapshot(std::array{Backend::Sqlite});
    PhysicalTable parent;
    parent.name = "rebuild_parent";
    parent.columns = {{.name = "id",
                       .logicalType = orm::model::ColumnType::Int,
                       .sqlType = "INTEGER",
                       .nullable = false,
                       .maxStringLength = {},
                       .autoIncrement = true}};
    parent.primaryKey = {"id"};
    parent.primaryKeyName = "orm_pk_parent";
    PhysicalTable child;
    child.name = "rebuild_child";
    child.columns = {{.name = "id",
                      .logicalType = orm::model::ColumnType::Int,
                      .sqlType = "INTEGER",
                      .nullable = false,
                      .maxStringLength = {},
                      .autoIncrement = false},
                     {.name = "parent_id",
                      .logicalType = orm::model::ColumnType::Int,
                      .sqlType = "INTEGER",
                      .nullable = true,
                      .maxStringLength = {},
                      .autoIncrement = false}};
    child.primaryKey = {"id"};
    child.primaryKeyName = "orm_pk_child";
    child.foreignKeys = {{.name = "orm_fk_child",
                          .columns = {"parent_id"},
                          .targetTable = parent.name,
                          .targetColumns = {"id"},
                          .onDelete = "CASCADE",
                          .onUpdate = "NO ACTION"}};
    snapshot.backends.at(Backend::Sqlite).tables = {parent, child};
    detail::canonicalize(snapshot);
    return snapshot;
}
auto nextSnapshot(const SchemaSnapshot& before) -> SchemaSnapshot
{
    auto after = before;
    auto& parent =
        *std::ranges::find(after.backends.at(Backend::Sqlite).tables, "rebuild_parent", &PhysicalTable::name);
    parent.columns.push_back({.name = "label",
                              .logicalType = orm::model::ColumnType::String,
                              .sqlType = "TEXT",
                              .nullable = false,
                              .maxStringLength = {},
                              .autoIncrement = false});
    detail::canonicalize(after);
    return after;
}
auto rebuildMigration(const SchemaSnapshot& before) -> Migration
{
    GenerationDecisions decisions;
    decisions.backfills.push_back({.table = "rebuild_parent",
                                   .column = "label",
                                   .constant = GenerationLiteral{std::string{"filled"}},
                                   .sourceColumn = {},
                                   .expressions = {}});
    return generate(2, "required_label", compare(before, nextSnapshot(before), decisions), decisions).migration;
}
auto setup(orm::Database& database, const SchemaSnapshot& before) -> void
{
    database.connect("sqlite3://:memory:");
    auto initial = generate(1, "initial", compare(emptySnapshot(std::array{Backend::Sqlite}), before)).migration;
    Runner{database, Catalog{initial}}.up();
    auto& session = Access::session(database);
    session << "INSERT INTO rebuild_parent(id) VALUES(1)";
    session << "INSERT INTO rebuild_child VALUES(9,1)";
}
auto expectInvalid(orm::Database& database, const SchemaSnapshot& before) -> void
{
    const auto first = generate(1, "initial", compare(emptySnapshot(std::array{Backend::Sqlite}), before)).migration;
    try
    {
        Runner{database, Catalog{first, rebuildMigration(before)}}.up();
        FAIL() << "Expected source preflight rejection";
    }
    catch (const MigrationError& error)
    {
        EXPECT_EQ(error.getCode(), ErrorCode::InvalidSchema) << error.what();
    }
    int foreignKeys{};
    Access::session(database) << "PRAGMA foreign_keys", soci::into(foreignKeys);
    EXPECT_EQ(foreignKeys, 1);
    EXPECT_FALSE(Access::active(database));
}
}

TEST(MigrationRebuildTest, PreservesReferencingRowsBackfillAndDeletedIdentityHighwater)
{
    const auto before = sourceSnapshot();
    orm::Database database;
    setup(database, before);
    auto& session = Access::session(database);
    session << "INSERT INTO rebuild_parent(id) VALUES(500)";
    session << "DELETE FROM rebuild_parent WHERE id=500";
    const auto first = generate(1, "initial", compare(emptySnapshot(std::array{Backend::Sqlite}), before)).migration;
    const auto change = rebuildMigration(before);
    ASSERT_EQ(change.scripts.at(Backend::Sqlite).upContext.mode, MigrationExecutionMode::SqliteRebuild);
    Runner runner{database, Catalog{first, change}};
    runner.up();
    int parentId{}, foreignKeys{};
    std::string label;
    session << "SELECT parent_id FROM rebuild_child WHERE id=9", soci::into(parentId);
    session << "SELECT label FROM rebuild_parent WHERE id=1", soci::into(label);
    EXPECT_EQ(parentId, 1);
    EXPECT_EQ(label, "filled");
    session << "INSERT INTO rebuild_parent(label) VALUES('new')";
    long long identity{};
    session << "SELECT MAX(id) FROM rebuild_parent", soci::into(identity);
    EXPECT_EQ(identity, 501);
    session << "PRAGMA foreign_keys", soci::into(foreignKeys);
    EXPECT_EQ(foreignKeys, 1);
    EXPECT_EQ(runner.status().currentVersion, 2);
}

TEST(MigrationRebuildTest, PreservesEmptyIdentityTableAndMaximumHighwaterWithoutOverflow)
{
    for (const auto highwater : {0LL, 800LL, (std::numeric_limits<long long>::max)()})
    {
        const auto before = sourceSnapshot();
        orm::Database database;
        setup(database, before);
        auto& session = Access::session(database);
        session << "DELETE FROM rebuild_child";
        session << "DELETE FROM rebuild_parent";
        session << "DELETE FROM sqlite_sequence";
        if (highwater != 0)
        {
            session << "INSERT INTO sqlite_sequence(name,seq) VALUES('rebuild_parent',:seq)",
                soci::use(highwater, "seq");
        }
        const auto first =
            generate(1, "initial", compare(emptySnapshot(std::array{Backend::Sqlite}), before)).migration;
        Runner{database, Catalog{first, rebuildMigration(before)}}.up();
        long long actual{};
        session << "SELECT seq FROM sqlite_sequence WHERE name='rebuild_parent'", soci::into(actual);
        EXPECT_EQ(actual, highwater);
        int count{};
        session << "SELECT COUNT(*) FROM rebuild_parent", soci::into(count);
        EXPECT_EQ(count, 0);
    }
}

TEST(MigrationRebuildTest, RejectsUnmanagedObjectsAndSourceDriftBeforeAnyChanges)
{
    for (const auto sql :
         {"CREATE INDEX custom_index ON rebuild_parent(id)", "CREATE VIEW custom_view AS SELECT * FROM rebuild_parent",
          "CREATE TRIGGER custom_trigger AFTER INSERT ON rebuild_parent BEGIN SELECT 1; END",
          "ALTER TABLE rebuild_parent ADD COLUMN extra TEXT"})
    {
        const auto before = sourceSnapshot();
        orm::Database database;
        setup(database, before);
        Access::session(database) << sql;
        expectInvalid(database, before);
    }
}

TEST(MigrationRebuildTest, RejectsStagingCollisionAndRestoresForeignKeys)
{
    const auto before = sourceSnapshot();
    orm::Database database;
    setup(database, before);
    const auto change = rebuildMigration(before);
    const auto& stage = change.scripts.at(Backend::Sqlite).upContext.rebuildTables.front().staging;
    Access::session(database) << "CREATE TABLE " + detail::quoteIdentifier(Backend::Sqlite, stage) + "(id INT)";
    expectInvalid(database, before);
}

TEST(MigrationRebuildTest, ForeignKeyCheckRollsBackAndRestoresSession)
{
    auto before = sourceSnapshot();
    orm::Database database;
    setup(database, before);
    auto& session = Access::session(database);
    session << "PRAGMA foreign_keys=OFF";
    session << "INSERT INTO rebuild_child VALUES(10,999)";
    session << "PRAGMA foreign_keys=ON";
    expectInvalid(database, before);
    int columns{};
    session << "SELECT COUNT(*) FROM pragma_table_info('rebuild_parent') WHERE name='label'", soci::into(columns);
    EXPECT_EQ(columns, 0);
}

TEST(MigrationRebuildTest, PreservesDisabledForeignKeySetting)
{
    const auto before = sourceSnapshot();
    orm::Database database;
    setup(database, before);
    auto& session = Access::session(database);
    session << "PRAGMA foreign_keys=OFF";
    const auto first = generate(1, "initial", compare(emptySnapshot(std::array{Backend::Sqlite}), before)).migration;
    Runner{database, Catalog{first, rebuildMigration(before)}}.up();
    int foreignKeys{};
    session << "PRAGMA foreign_keys", soci::into(foreignKeys);
    EXPECT_EQ(foreignKeys, 0);
}

TEST(MigrationRebuildTest, RejectsEvenEmptyUnmanagedInboundForeignKey)
{
    const auto before = sourceSnapshot();
    orm::Database database;
    setup(database, before);
    Access::session(database) << "CREATE TABLE unmanaged_child(parent_id INTEGER REFERENCES rebuild_parent(id))";
    expectInvalid(database, before);
}

TEST(MigrationRebuildTest, DetectsMissingAndAlteredManagedIndexes)
{
    for (const auto alteration : {"", " DESC", " COLLATE NOCASE"})
    {
        auto before = sourceSnapshot();
        auto& parent =
            *std::ranges::find(before.backends.at(Backend::Sqlite).tables, "rebuild_parent", &PhysicalTable::name);
        parent.indexes.push_back({.name = "managed_index", .columns = {"id"}, .unique = false});
        orm::Database database;
        setup(database, before);
        auto& session = Access::session(database);
        session << "DROP INDEX managed_index";
        if (!std::string_view{alteration}.empty())
            session << "CREATE INDEX managed_index ON rebuild_parent(id" + std::string{alteration} + ")";
        expectInvalid(database, before);
    }
}

TEST(MigrationRebuildTest, QuotedTableNameWithParenthesisCanBeRebuilt)
{
    auto before = sourceSnapshot();
    const std::string name = "parent(with_\"quote";
    auto& parent =
        *std::ranges::find(before.backends.at(Backend::Sqlite).tables, "rebuild_parent", &PhysicalTable::name);
    parent.name = name;
    auto& child = *std::ranges::find(before.backends.at(Backend::Sqlite).tables, "rebuild_child", &PhysicalTable::name);
    child.foreignKeys.front().targetTable = name;
    detail::canonicalize(before);
    auto after = before;
    auto& target = *std::ranges::find(after.backends.at(Backend::Sqlite).tables, name, &PhysicalTable::name);
    target.columns.push_back(
        {.name = "label", .logicalType = orm::model::ColumnType::String, .sqlType = "TEXT", .nullable = true});
    detail::canonicalize(after);
    const auto first =
        generate(1, "quoted_initial", compare(emptySnapshot(std::array{Backend::Sqlite}), before)).migration;
    const auto change = generate(2, "quoted_change", compare(before, after)).migration;
    orm::Database database;
    database.connect("sqlite3://:memory:");
    Runner{database, Catalog{first}}.up();
    auto& session = Access::session(database);
    session << "INSERT INTO " + detail::quoteIdentifier(Backend::Sqlite, name) + "(id) VALUES(1)";
    Runner runner{database, Catalog{first, change}};
    ASSERT_NO_THROW(runner.up());
    EXPECT_EQ(runner.status().currentVersion, 2);
}

TEST(MigrationRebuildTest, BeginFailureRestoresForeignKeysAndOwnership)
{
    const auto before = sourceSnapshot();
    const auto first = generate(1, "initial", compare(emptySnapshot(std::array{Backend::Sqlite}), before)).migration;
    const auto path = std::filesystem::temp_directory_path() /
                      ("orm_rebuild_lock_" +
                       std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()) + ".db");
    struct Cleanup
    {
        std::filesystem::path path;
        ~Cleanup()
        {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    } cleanup{path};
    orm::Database database, locker;
    const auto url = "sqlite3://" + path.string();
    database.connect(url);
    Runner{database, Catalog{first}}.up();
    locker.connect(url);
    Access::session(locker) << "BEGIN IMMEDIATE";
    Access::session(database) << "PRAGMA busy_timeout=0";
    try
    {
        Runner{database, Catalog{first, rebuildMigration(before)}}.up();
        FAIL();
    }
    catch (const MigrationError& error)
    {
        EXPECT_EQ(error.getCode(), ErrorCode::LockUnavailable) << error.what();
    }
    int foreignKeys{};
    Access::session(database) << "PRAGMA foreign_keys", soci::into(foreignKeys);
    EXPECT_EQ(foreignKeys, 1);
    EXPECT_FALSE(Access::active(database));
    Access::session(locker) << "ROLLBACK";
    Runner{database, Catalog{first, rebuildMigration(before)}}.up();
}

TEST(MigrationRebuildTest, PreservesSetNullChildrenAndSelfReferences)
{
    auto before = sourceSnapshot();
    auto& tables = before.backends.at(Backend::Sqlite).tables;
    auto& parent = *std::ranges::find(tables, "rebuild_parent", &PhysicalTable::name);
    auto& child = *std::ranges::find(tables, "rebuild_child", &PhysicalTable::name);
    child.foreignKeys.front().onDelete = "SET NULL";
    parent.columns.push_back({.name = "previous_id",
                              .logicalType = orm::model::ColumnType::Int,
                              .sqlType = "INTEGER",
                              .nullable = true,
                              .maxStringLength = {},
                              .autoIncrement = false});
    parent.foreignKeys.push_back({.name = "orm_fk_self",
                                  .columns = {"previous_id"},
                                  .targetTable = parent.name,
                                  .targetColumns = {"id"},
                                  .onDelete = "NO ACTION",
                                  .onUpdate = "NO ACTION"});
    detail::canonicalize(before);
    orm::Database database;
    setup(database, before);
    auto& session = Access::session(database);
    session << "UPDATE rebuild_parent SET previous_id=1";
    const auto first = generate(1, "initial", compare(emptySnapshot(std::array{Backend::Sqlite}), before)).migration;
    Runner{database, Catalog{first, rebuildMigration(before)}}.up();
    int actual{};
    session << "SELECT parent_id FROM rebuild_child WHERE id=9", soci::into(actual);
    EXPECT_EQ(actual, 1);
    session << "SELECT previous_id FROM rebuild_parent WHERE id=1", soci::into(actual);
    EXPECT_EQ(actual, 1);
}

TEST(MigrationRebuildTest, CopiesAllCyclicTablesBeforeDroppingAnySource)
{
    auto before = sourceSnapshot();
    auto& tables = before.backends.at(Backend::Sqlite).tables;
    auto& parent = *std::ranges::find(tables, "rebuild_parent", &PhysicalTable::name);
    parent.columns.push_back({.name = "child_id",
                              .logicalType = orm::model::ColumnType::Int,
                              .sqlType = "INTEGER",
                              .nullable = true,
                              .maxStringLength = {},
                              .autoIncrement = false});
    parent.foreignKeys.push_back({.name = "orm_fk_parent_child",
                                  .columns = {"child_id"},
                                  .targetTable = "rebuild_child",
                                  .targetColumns = {"id"},
                                  .onDelete = "SET NULL",
                                  .onUpdate = "NO ACTION"});
    detail::canonicalize(before);
    orm::Database database;
    setup(database, before);
    auto& session = Access::session(database);
    session << "UPDATE rebuild_parent SET child_id=9";
    auto after = nextSnapshot(before);
    auto& child = *std::ranges::find(after.backends.at(Backend::Sqlite).tables, "rebuild_child", &PhysicalTable::name);
    child.columns.push_back({.name = "label",
                             .logicalType = orm::model::ColumnType::String,
                             .sqlType = "TEXT",
                             .nullable = false,
                             .maxStringLength = {},
                             .autoIncrement = false});
    detail::canonicalize(after);
    GenerationDecisions decisions;
    for (const auto table : {"rebuild_parent", "rebuild_child"})
    {
        decisions.backfills.push_back({.table = table,
                                       .column = "label",
                                       .constant = GenerationLiteral{std::string{"cycle"}},
                                       .sourceColumn = {},
                                       .expressions = {}});
    }
    const auto change = generate(2, "cyclic", compare(before, after, decisions), decisions).migration;
    ASSERT_EQ(change.scripts.at(Backend::Sqlite).upContext.rebuildTables.size(), 2);
    const auto first = generate(1, "initial", compare(emptySnapshot(std::array{Backend::Sqlite}), before)).migration;
    Runner{database, Catalog{first, change}}.up();
    int parentId{}, childId{};
    session << "SELECT parent_id FROM rebuild_child WHERE id=9", soci::into(parentId);
    session << "SELECT child_id FROM rebuild_parent WHERE id=1", soci::into(childId);
    EXPECT_EQ(parentId, 1);
    EXPECT_EQ(childId, 9);
}
