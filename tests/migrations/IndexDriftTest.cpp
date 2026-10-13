module;

#include "tests/UnitTestPrelude.hpp"

module orm;

import :internal;
import :test_support;
import :migrations_internal;

namespace migration_index_models
{
struct Record
{
    int id;
    std::optional<std::string> label;
    inline static constexpr orm::reflection::FixedString table_name{"migration_index_drift"};
    inline static constexpr auto auto_increment_columns = orm::autoIncrement<&Record::id>();
    inline static constexpr auto string_lengths = orm::stringLengths(orm::stringLength<&Record::label, 32>());
};
struct Extended
{
    int id;
    std::optional<std::string> label;
    std::optional<int> extra;
    inline static constexpr orm::reflection::FixedString table_name{"migration_index_drift"};
    inline static constexpr auto auto_increment_columns = orm::autoIncrement<&Extended::id>();
    inline static constexpr auto string_lengths = orm::stringLengths(orm::stringLength<&Extended::label, 32>());
};
using Before = orm::Schema<Record>;
using After = orm::Schema<Extended>;
} // namespace migration_index_models

namespace
{
using namespace orm::migrations;
using Backend = orm::db::BackendType;
namespace index_detail = orm::migrations::detail;
} // namespace

class MigrationIndexDriftTest : public DatabaseTest<migration_index_models::Before>
{
public:
    auto session() -> soci::session&
    {
        return index_detail::DatabaseAccess::session(connection);
    }
    auto quote(std::string_view name) const -> std::string
    {
        return index_detail::quoteIdentifier(GetParam().type, name);
    }
    auto family() const -> bool
    {
        return GetParam().type == Backend::Mysql || GetParam().type == Backend::Mariadb;
    }
    auto prepare(bool unique = false) -> Catalog
    {
        const auto indexed = [&](orm::model::SchemaView models)
        {
            auto snapshot = capture(models, std::array{GetParam().type});
            auto& table = snapshot.backends.at(GetParam().type).tables.front();
            // Use text on PostgreSQL to exercise its text operator classes;
            // MySQL-family indexes require the bounded VARCHAR definition.
            if (GetParam().type == Backend::Postgres)
            {
                auto& label = *std::ranges::find(table.columns, "label", &PhysicalColumn::name);
                label.sqlType = "TEXT";
                label.maxStringLength.reset();
            }
            table.indexes.push_back({.name = "managed_label", .columns = {"label"}, .unique = unique});
            index_detail::canonicalize(snapshot);
            return snapshot;
        };
        const auto before = indexed(migration_index_models::Before::view);
        const auto after = indexed(migration_index_models::After::view);
        const auto first =
            generate(1, "index_initial", compare(emptySnapshot(std::array{GetParam().type}), before)).migration;
        const auto second = generate(2, "index_extra", compare(before, after)).migration;
        Runner{connection, Catalog{first}}.up();
        session() << "INSERT INTO " + quote("migration_index_drift") + " (id,label) VALUES(1,'alpha')";
        return Catalog{first, second};
    }
    auto dropIndex() -> void
    {
        session() << "DROP INDEX " + quote("managed_label") + (family() ? " ON " + quote("migration_index_drift") : "");
    }
    auto replaceIndex(std::string columns, std::string suffix = {}, bool unique = false,
                      std::string method = {}) -> void
    {
        dropIndex();
        session() << std::string{unique ? "CREATE UNIQUE INDEX " : "CREATE INDEX "} + quote("managed_label") + " ON " +
                         quote("migration_index_drift") + method + " (" + columns + ")" + suffix;
    }
    auto expectRejected(const Catalog& catalog, std::string expectedLabel = "alpha") -> void
    {
        Runner runner{connection, catalog};
        try
        {
            runner.up();
            FAIL() << "A drifted source definition must fail before the migration changes its table";
        }
        catch (const MigrationError& error)
        {
            EXPECT_EQ(error.getCode(), ErrorCode::InvalidSchema)
                << error.what() << "; native code: " << error.getNativeCode().value_or("none");
            EXPECT_EQ(error.getVersion(), 2);
            EXPECT_EQ(error.getBackendType(), GetParam().type);
        }
        const auto state = runner.status();
        EXPECT_TRUE(state.isValid());
        EXPECT_EQ(state.currentVersion, 1);
        EXPECT_EQ(state.applied.size(), 1);
        EXPECT_FALSE(state.incomplete);
        EXPECT_TRUE(connection.isConnected());
        EXPECT_FALSE(index_detail::DatabaseAccess::active(connection));
        int extraColumns{}, rows{};
        if (GetParam().type == Backend::Sqlite)
            session() << "SELECT COUNT(*) FROM pragma_table_info('migration_index_drift') WHERE name='extra'",
                soci::into(extraColumns);
        else
            session() << "SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=" +
                             std::string{GetParam().type == Backend::Postgres ? "current_schema()" : "DATABASE()"} +
                             " AND table_name='migration_index_drift' AND column_name='extra'",
                soci::into(extraColumns);
        session() << "SELECT COUNT(*) FROM " + quote("migration_index_drift"), soci::into(rows);
        std::string label;
        session() << "SELECT label FROM " + quote("migration_index_drift") + " WHERE id=1", soci::into(label);
        EXPECT_EQ(extraColumns, 0);
        EXPECT_EQ(rows, 1);
        EXPECT_EQ(label, expectedLabel);
    }
};

TEST_P(MigrationIndexDriftTest, UnchangedManagedIndexAllowsTheMigration)
{
    const auto catalog = prepare();
    Runner runner{connection, catalog};
    try
    {
        runner.up();
    }
    catch (const MigrationError& error)
    {
        FAIL() << error.what() << "; migration code: " << static_cast<int>(error.getCode())
               << "; native code: " << error.getNativeCode().value_or("none") << "; version: " << error.getVersion();
    }
    EXPECT_EQ(runner.status().currentVersion, 2);
    std::string label;
    session() << "SELECT label FROM " + quote("migration_index_drift") + " WHERE id=1", soci::into(label);
    EXPECT_EQ(label, "alpha");
}

TEST_P(MigrationIndexDriftTest, MissingIndexDoesNotAdvanceHistoryOrModifyRows)
{
    const auto catalog = prepare();
    dropIndex();
    expectRejected(catalog);
}

TEST_P(MigrationIndexDriftTest, DescendingIndexDoesNotAdvanceHistoryOrModifyRows)
{
    const auto catalog = prepare();
    replaceIndex("label DESC");
    expectRejected(catalog);
}

TEST_P(MigrationIndexDriftTest, ChangedUniquenessDoesNotAdvanceHistoryOrModifyRows)
{
    const auto catalog = prepare();
    replaceIndex("label", {}, true);
    expectRejected(catalog);
}

TEST_P(MigrationIndexDriftTest, ChangedIndexColumnsDoNotAdvanceHistoryOrModifyRows)
{
    const auto catalog = prepare();
    replaceIndex("id");
    expectRejected(catalog);
}

TEST_P(MigrationIndexDriftTest, EmptyUnmanagedIncomingForeignKeyBlocksTheMigration)
{
    const auto catalog = prepare();
    session() << "CREATE TABLE " + quote("unmanaged_incoming") +
                     " (id INTEGER PRIMARY KEY,record_id INTEGER,CONSTRAINT " + quote("unmanaged_incoming_fk") +
                     " FOREIGN KEY(record_id) REFERENCES " + quote("migration_index_drift") + "(id))" +
                     (family() ? " ENGINE=InnoDB" : "");
    expectRejected(catalog);
    int children{};
    session() << "SELECT COUNT(*) FROM " + quote("unmanaged_incoming"), soci::into(children);
    EXPECT_EQ(children, 0);
}

TEST_P(MigrationIndexDriftTest, PartialIndexDoesNotAdvanceHistoryOrModifyRows)
{
    if (family())
        GTEST_SKIP() << "This backend does not support partial indexes";
    const auto catalog = prepare();
    replaceIndex("label", " WHERE label IS NOT NULL");
    expectRejected(catalog);
}

TEST_P(MigrationIndexDriftTest, ExpressionIndexDoesNotAdvanceHistoryOrModifyRows)
{
    if (family())
        GTEST_SKIP() << "Expression index syntax is specific to the native server";
    const auto catalog = prepare();
    replaceIndex("lower(label)");
    expectRejected(catalog);
}

TEST_P(MigrationIndexDriftTest, NondefaultPostgresqlOperatorClassIsRejected)
{
    if (GetParam().type != Backend::Postgres)
        GTEST_SKIP() << "PostgreSQL operator class regression";
    const auto catalog = prepare();
    replaceIndex("label text_pattern_ops");
    expectRejected(catalog);
}

TEST_P(MigrationIndexDriftTest, PostgresqlHashIndexIsRejected)
{
    if (GetParam().type != Backend::Postgres)
        GTEST_SKIP() << "PostgreSQL access method regression";
    const auto catalog = prepare();
    replaceIndex("label", {}, false, " USING HASH");
    expectRejected(catalog);
}

TEST_P(MigrationIndexDriftTest, NondefaultPostgresqlIndexCollationIsRejected)
{
    if (GetParam().type != Backend::Postgres)
        GTEST_SKIP() << "PostgreSQL index collation regression";
    const auto catalog = prepare();
    replaceIndex("label COLLATE \"C\"");
    expectRejected(catalog);
}

TEST_P(MigrationIndexDriftTest, PostgresqlNullsFirstIsRejected)
{
    if (GetParam().type != Backend::Postgres)
        GTEST_SKIP() << "PostgreSQL NULLS FIRST regression";
    const auto catalog = prepare();
    replaceIndex("label NULLS FIRST");
    expectRejected(catalog);
}

TEST_P(MigrationIndexDriftTest, PostgresqlIncludedColumnIsRejected)
{
    if (GetParam().type != Backend::Postgres)
        GTEST_SKIP() << "PostgreSQL INCLUDE regression";
    const auto catalog = prepare();
    replaceIndex("label", " INCLUDE(id)");
    expectRejected(catalog);
}

TEST_P(MigrationIndexDriftTest, PostgresqlNullsNotDistinctIsRejected)
{
    if (GetParam().type != Backend::Postgres)
        GTEST_SKIP() << "PostgreSQL NULLS NOT DISTINCT regression";
    const auto catalog = prepare(true);
    replaceIndex("label", " NULLS NOT DISTINCT", true);
    expectRejected(catalog);
}

TEST_P(MigrationIndexDriftTest, InvisibleOrIgnoredFamilyIndexIsRejected)
{
    if (!family())
        GTEST_SKIP() << "MySQL-family index visibility regression";
    const auto catalog = prepare();
    session() << "ALTER TABLE " + quote("migration_index_drift") + " ALTER INDEX " + quote("managed_label") +
                     (GetParam().type == Backend::Mysql ? " INVISIBLE" : " IGNORED");
    expectRejected(catalog);
}

TEST_P(MigrationIndexDriftTest, FamilyIndexPrefixIsRejected)
{
    if (!family())
        GTEST_SKIP() << "MySQL-family prefix index regression";
    const auto catalog = prepare();
    replaceIndex("label(4)");
    expectRejected(catalog);
}

TEST_P(MigrationIndexDriftTest, ChangedFamilyStorageEngineIsRejected)
{
    if (!family())
        GTEST_SKIP() << "MySQL-family table engine regression";
    const auto catalog = prepare();
    session() << "ALTER TABLE " + quote("migration_index_drift") + " ENGINE=MEMORY";
    expectRejected(catalog);
}

TEST_P(MigrationIndexDriftTest, ChangedFamilyTableCollationIsRejected)
{
    if (!family())
        GTEST_SKIP() << "MySQL-family table collation regression";
    const auto catalog = prepare();
    session() << "ALTER TABLE " + quote("migration_index_drift") + " COLLATE=utf8mb4_general_ci";
    expectRejected(catalog);
}

TEST_P(MigrationIndexDriftTest, ExplicitFamilyTableOptionsAreRejected)
{
    if (!family())
        GTEST_SKIP() << "MySQL-family explicit create options regression";
    const auto catalog = prepare();
    session() << "ALTER TABLE " + quote("migration_index_drift") + " ROW_FORMAT=DYNAMIC";
    expectRejected(catalog);
}

TEST_P(MigrationIndexDriftTest, ChangedColumnCollationIsRejected)
{
    if (GetParam().type == Backend::Sqlite)
        GTEST_SKIP() << "SQLite column definitions are checked against stored CREATE text";
    const auto catalog = prepare();
    if (GetParam().type == Backend::Postgres)
        session() << "ALTER TABLE " + quote("migration_index_drift") + " ALTER COLUMN label TYPE TEXT COLLATE \"C\"";
    else
        session() << "ALTER TABLE " + quote("migration_index_drift") +
                         " MODIFY COLUMN label VARCHAR(32) CHARACTER SET utf8mb4 COLLATE utf8mb4_general_ci NULL";
    expectRejected(catalog);
}

TEST_P(MigrationIndexDriftTest, GeneratedColumnReplacementIsRejected)
{
    if (GetParam().type == Backend::Sqlite)
        GTEST_SKIP() << "SQLite generated definitions are checked against stored CREATE text";
    const auto catalog = prepare();
    dropIndex();
    session() << "ALTER TABLE " + quote("migration_index_drift") + " DROP COLUMN label";
    session() << "ALTER TABLE " + quote("migration_index_drift") + " ADD COLUMN label " +
                     (GetParam().type == Backend::Postgres ? "TEXT GENERATED ALWAYS AS (id::text) STORED" :
                                                             "VARCHAR(32) GENERATED ALWAYS AS ('1') STORED");
    session() << "CREATE INDEX " + quote("managed_label") + " ON " + quote("migration_index_drift") + " (label)";
    expectRejected(catalog, "1");
}

TEST_P(MigrationIndexDriftTest, ExplicitNullStringDefaultIsRejected)
{
    if (GetParam().type == Backend::Sqlite)
        GTEST_SKIP() << "SQLite cannot alter column defaults directly";
    const auto catalog = prepare();
    session() << "ALTER TABLE " + quote("migration_index_drift") + " ALTER COLUMN label SET DEFAULT 'NULL'";
    expectRejected(catalog);
}

TEST_P(MigrationIndexDriftTest, ExplicitCustomDefaultIsRejected)
{
    if (GetParam().type == Backend::Sqlite)
        GTEST_SKIP() << "SQLite cannot alter column defaults directly";
    const auto catalog = prepare();
    session() << "ALTER TABLE " + quote("migration_index_drift") + " ALTER COLUMN label SET DEFAULT 'custom'";
    expectRejected(catalog);
}

TEST_P(MigrationIndexDriftTest, PostgresqlAlwaysIdentityIsRejected)
{
    if (GetParam().type != Backend::Postgres)
        GTEST_SKIP() << "PostgreSQL identity generation regression";
    const auto catalog = prepare();
    session() << "ALTER TABLE " + quote("migration_index_drift") + " ALTER COLUMN id SET GENERATED ALWAYS";
    expectRejected(catalog);
}

INSTANTIATE_TEST_SUITE_P(DatabaseTest, MigrationIndexDriftTest, orm::test::fixtures::conformanceBackendTestConfigs,
                         orm::test::fixtures::backendTestName);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(MigrationIndexDriftTest);
