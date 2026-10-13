module;

#include <format>

#include "tests/UnitTestPrelude.hpp"

module orm;

import :internal;
import :test_support;
import :migrations_internal;

namespace runner_guard_models
{
struct Parent
{
    int first;
    int second;
    inline static constexpr orm::reflection::FixedString table_name{"runner_guard_parent"};
    inline static constexpr auto id_columns = orm::primaryKey<&Parent::first, &Parent::second>();
};
struct Child
{
    int id;
    std::optional<Parent> parent;
    std::optional<int> spare;
    inline static constexpr orm::reflection::FixedString table_name{"runner_guard_child"};
};
using Schema = orm::Schema<Parent, Child>;
} // namespace runner_guard_models

namespace
{
using namespace orm::migrations;
using Backend = orm::db::BackendType;
namespace guard_detail = orm::migrations::detail;
} // namespace

class MigrationRunnerGuardTest : public DatabaseTest<runner_guard_models::Schema>
{
public:
    SchemaSnapshot before;

    auto session() -> soci::session&
    {
        return guard_detail::DatabaseAccess::session(connection);
    }
    auto quote(std::string_view name) const -> std::string
    {
        return guard_detail::quoteIdentifier(GetParam().type, name);
    }
    auto family() const -> bool
    {
        return GetParam().type == Backend::Mysql || GetParam().type == Backend::Mariadb;
    }
    auto table(std::string_view name) const -> const PhysicalTable&
    {
        const auto& tables = before.backends.at(GetParam().type).tables;
        return *std::ranges::find(tables, name, &PhysicalTable::name);
    }
    auto prepare(bool child = true, bool quotedColumn = false) -> Catalog
    {
        before = capture(runner_guard_models::Schema::view, std::array{GetParam().type});
        auto& tables = before.backends.at(GetParam().type).tables;
        if (!child)
            std::erase_if(tables, [](const auto& candidate) { return candidate.name == "runner_guard_child"; });
        if (quotedColumn)
        {
            auto& parent = *std::ranges::find(tables, "runner_guard_parent", &PhysicalTable::name);
            std::ranges::find(parent.columns, "first", &PhysicalColumn::name)->name = "first\"part";
            parent.primaryKey.front() = "first\"part";
        }
        guard_detail::canonicalize(before);
        auto after = before;
        auto& parent =
            *std::ranges::find(after.backends.at(GetParam().type).tables, "runner_guard_parent", &PhysicalTable::name);
        parent.columns.push_back(
            {.name = "extra", .logicalType = orm::model::ColumnType::Int, .sqlType = "INTEGER", .nullable = true});
        guard_detail::canonicalize(after);
        const auto first =
            generate(1, "guard_initial", compare(emptySnapshot(std::array{GetParam().type}), before)).migration;
        const auto second = generate(2, "guard_extra", compare(before, after)).migration;
        Runner{connection, Catalog{first}}.up();
        session() << std::format("INSERT INTO {} ({},{}) VALUES(11,11)", quote("runner_guard_parent"),
                                 quote(table("runner_guard_parent").primaryKey[0]), quote("second"));
        if (child)
        {
            const auto& key = table("runner_guard_child").foreignKeys.front();
            session() << std::format("INSERT INTO {} ({},{},{},{}) VALUES(1,11,11,44)", quote("runner_guard_child"),
                                     quote("id"), quote(key.columns[0]), quote(key.columns[1]), quote("spare"));
        }
        return Catalog{first, second};
    }
    auto expectRejected(const Catalog& catalog, std::string_view message) -> void
    {
        Runner runner{connection, catalog};
        try
        {
            runner.up();
            FAIL() << "Source drift must be detected before DDL or history changes";
        }
        catch (const MigrationError& error)
        {
            EXPECT_EQ(error.getCode(), ErrorCode::InvalidSchema) << error.what();
            EXPECT_EQ(error.getVersion(), 2);
            EXPECT_NE(std::string{error.what()}.find(message), std::string::npos) << error.what();
        }
        const auto state = runner.status();
        EXPECT_TRUE(state.isValid());
        EXPECT_EQ(state.currentVersion, 1);
        EXPECT_FALSE(state.incomplete);
        int columns{}, rows{};
        if (GetParam().type == Backend::Sqlite)
            session() << "SELECT COUNT(*) FROM pragma_table_info('runner_guard_parent') WHERE name='extra'",
                soci::into(columns);
        else
            session() << "SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=" +
                             std::string{GetParam().type == Backend::Postgres ? "current_schema()" : "DATABASE()"} +
                             " AND table_name='runner_guard_parent' AND column_name='extra'",
                soci::into(columns);
        session() << "SELECT COUNT(*) FROM " + quote("runner_guard_parent"), soci::into(rows);
        EXPECT_EQ(columns, 0);
        EXPECT_EQ(rows, 1);
        EXPECT_FALSE(guard_detail::DatabaseAccess::active(connection));
    }
    auto dropForeignKey() -> void
    {
        session() << "ALTER TABLE " + quote("runner_guard_child") +
                         (family() ? " DROP FOREIGN KEY " : " DROP CONSTRAINT ") +
                         quote(table("runner_guard_child").foreignKeys.front().name);
    }
};

TEST_P(MigrationRunnerGuardTest, ReservedNativeStagingObjectsBlockBeforeTheJournalIsCreated)
{
    if (GetParam().type == Backend::Sqlite)
        GTEST_SKIP() << "Native reserved staging object preflight";
    const auto catalog = prepare();
    session() << "CREATE TABLE __orm_intruder(value INTEGER)";
    expectRejected(catalog, "Reserved migration staging");
}

TEST_P(MigrationRunnerGuardTest, MissingSourceColumnBlocksBeforeAnyTargetColumnIsAdded)
{
    if (GetParam().type == Backend::Sqlite)
        GTEST_SKIP() << "SQLite compares complete stored CREATE definitions";
    const auto catalog = prepare();
    session() << "ALTER TABLE " + quote("runner_guard_child") + " DROP COLUMN spare";
    expectRejected(catalog, "Source table columns differ");
}

TEST_P(MigrationRunnerGuardTest, ChangedPrimaryKeyOrderIsRejected)
{
    if (GetParam().type == Backend::Sqlite)
        GTEST_SKIP() << "Native constraint replacement regression";
    const auto catalog = prepare(false);
    session() << "ALTER TABLE " + quote("runner_guard_parent") +
                     (family() ? " DROP PRIMARY KEY" :
                                 " DROP CONSTRAINT " + quote(table("runner_guard_parent").primaryKeyName));
    session() << "ALTER TABLE " + quote("runner_guard_parent") + " ADD " +
                     (family() ? std::string{"PRIMARY KEY"} :
                                 "CONSTRAINT " + quote(table("runner_guard_parent").primaryKeyName) + " PRIMARY KEY") +
                     "(second,first)";
    expectRejected(catalog, "Source primary key differs");
}

TEST_P(MigrationRunnerGuardTest, MissingForeignKeyIsRejectedEvenWhenItsSupportingIndexRemains)
{
    if (GetParam().type == Backend::Sqlite)
        GTEST_SKIP() << "Native constraint replacement regression";
    const auto catalog = prepare();
    dropForeignKey();
    expectRejected(catalog, "Source foreign key is missing");
}

TEST_P(MigrationRunnerGuardTest, ChangedForeignKeyTargetOrderIsRejected)
{
    if (GetParam().type == Backend::Sqlite)
        GTEST_SKIP() << "Native constraint replacement regression";
    const auto catalog = prepare();
    dropForeignKey();
    if (family())
        session() << "CREATE UNIQUE INDEX guard_reverse_key ON " + quote("runner_guard_parent") + "(second,first)";
    auto key = table("runner_guard_child").foreignKeys.front();
    std::ranges::reverse(key.targetColumns);
    session() << "ALTER TABLE " + quote("runner_guard_child") + " ADD " +
                     guard_detail::renderForeignKey(GetParam().type, key);
    expectRejected(catalog, "Source foreign key differs");
}

TEST_P(MigrationRunnerGuardTest, ChangedForeignKeyDeleteActionIsRejected)
{
    if (GetParam().type == Backend::Sqlite)
        GTEST_SKIP() << "Native constraint replacement regression";
    const auto catalog = prepare();
    dropForeignKey();
    auto key = table("runner_guard_child").foreignKeys.front();
    key.onDelete = key.onDelete == "CASCADE" ? "RESTRICT" : "CASCADE";
    session() << "ALTER TABLE " + quote("runner_guard_child") + " ADD " +
                     guard_detail::renderForeignKey(GetParam().type, key);
    expectRejected(catalog, "Source foreign key actions differ");
}

TEST_P(MigrationRunnerGuardTest, DeferredPrimaryKeyOptionsAreRejected)
{
    if (GetParam().type != Backend::Postgres)
        GTEST_SKIP() << "PostgreSQL deferred constraint regression";
    const auto catalog = prepare(false);
    const auto key = quote(table("runner_guard_parent").primaryKeyName);
    session() << "ALTER TABLE runner_guard_parent DROP CONSTRAINT " + key;
    session() << "ALTER TABLE runner_guard_parent ADD CONSTRAINT " + key +
                     " PRIMARY KEY(first,second) DEFERRABLE INITIALLY DEFERRED";
    expectRejected(catalog, "Source constraint options differ");
}

TEST_P(MigrationRunnerGuardTest, QuotedSQLiteColumnNamesSurviveSourceDefinitionComparison)
{
    if (GetParam().type != Backend::Sqlite)
        GTEST_SKIP() << "SQLite stored CREATE parser regression";
    const auto catalog = prepare(false, true);
    Runner runner{connection, catalog};
    ASSERT_NO_THROW(runner.up());
    EXPECT_EQ(runner.status().currentVersion, 2);
    int value{};
    session() << "SELECT \"first\"\"part\" FROM runner_guard_parent", soci::into(value);
    EXPECT_EQ(value, 11);
}

TEST_P(MigrationRunnerGuardTest, UnmanagedSQLiteStrictOptionIsRejected)
{
    if (GetParam().type != Backend::Sqlite)
        GTEST_SKIP() << "SQLite stored CREATE options regression";
    const auto catalog = prepare(false);
    auto ddl = guard_detail::renderCreateTable(Backend::Sqlite, table("runner_guard_parent"));
    ddl.pop_back();
    ddl += " STRICT";
    session() << "DROP TABLE runner_guard_parent";
    session() << ddl;
    session() << "INSERT INTO runner_guard_parent VALUES(11,11)";
    expectRejected(catalog, "Source table differs");
}

INSTANTIATE_TEST_SUITE_P(DatabaseTest, MigrationRunnerGuardTest, orm::test::fixtures::conformanceBackendTestConfigs,
                         orm::test::fixtures::backendTestName);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(MigrationRunnerGuardTest);
