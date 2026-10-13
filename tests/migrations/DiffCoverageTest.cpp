module;

#include "tests/UnitTestPrelude.hpp"

module orm;

import :migrations_internal;

namespace migration_diff_models
{
struct Record
{
    int id;
    int value;
    inline static constexpr orm::reflection::FixedString table_name{"diff_values"};
};
using Schema = orm::Schema<Record>;
} // namespace migration_diff_models

namespace
{
using namespace orm::migrations;
using Backend = orm::db::BackendType;
using Type = orm::model::ColumnType;

auto diffSnapshot(Backend backend = Backend::Sqlite) -> SchemaSnapshot
{
    return capture(migration_diff_models::Schema::view, std::array{backend});
}
auto valueColumn(SchemaSnapshot& snapshot, Backend backend) -> PhysicalColumn&
{
    auto& columns = snapshot.backends.at(backend).tables.front().columns;
    return *std::ranges::find(columns, "value", &PhysicalColumn::name);
}
auto assertTypePolicy(Backend backend, Type sourceType, std::string sourceSql, Type targetType, std::string targetSql,
                      bool safe) -> void
{
    auto before = diffSnapshot(backend);
    auto& source = valueColumn(before, backend);
    source.logicalType = sourceType;
    source.sqlType = std::move(sourceSql);
    auto after = before;
    auto& target = valueColumn(after, backend);
    target.logicalType = targetType;
    target.sqlType = std::move(targetSql);
    const auto diff = compare(before, after);
    EXPECT_TRUE(diff.diagnostics.empty());
    ASSERT_EQ(diff.changes.size(), 1U);
    const auto& change = diff.changes.front();
    EXPECT_EQ(change.kind, SchemaChangeKind::AlterColumn);
    EXPECT_EQ(change.dataLoss, !safe);
    EXPECT_EQ(change.unresolved, !safe);
    if (!safe)
    {
        GenerationDecisions decisions;
        decisions.conversions.push_back(
            {.table = "diff_values", .column = "value", .constant = GenerationLiteral{std::int64_t{0}}});
        decisions.allowDataLossUp.push_back(change.id);
        const auto accepted = compare(before, after, decisions);
        ASSERT_EQ(accepted.changes.size(), 1U);
        EXPECT_TRUE(accepted.changes.front().dataLoss);
        EXPECT_FALSE(accepted.changes.front().unresolved);
        EXPECT_TRUE(accepted.diagnostics.empty());
    }
}
} // namespace

TEST(MigrationDiffCoverageTest, NumericWidthsSignednessAndFloatPrecisionRequireAppropriateDecisions)
{
    struct Case
    {
        Type source;
        std::string_view sourceSql;
        Type target;
        std::string_view targetSql;
        bool safe;
    };
    const std::array cases{
        Case{Type::Bool, "TINYINT", Type::Char, "SMALLINT", true},
        Case{Type::Char, "TINYINT", Type::UnsignedChar, "TINYINT UNSIGNED", false},
        Case{Type::UnsignedChar, "TINYINT UNSIGNED", Type::Short, "SMALLINT", true},
        Case{Type::Short, "SMALLINT", Type::UnsignedInt, "INT UNSIGNED", false},
        Case{Type::UnsignedShort, "SMALLINT UNSIGNED", Type::Int, "INT", true},
        Case{Type::UnsignedShort, "SMALLINT UNSIGNED", Type::UnsignedInt, "INT UNSIGNED", true},
        Case{Type::UnsignedInt, "INT UNSIGNED", Type::LongLong, "BIGINT", true},
        Case{Type::LongLong, "BIGINT", Type::Int, "INT", false},
        Case{Type::LongLong, "BIGINT", Type::UnsignedLongLong, "BIGINT UNSIGNED", false},
        Case{Type::UnsignedLongLong, "BIGINT UNSIGNED", Type::LongLong, "BIGINT", false},
        Case{Type::Float, "DOUBLE", Type::Double, "DOUBLE", true},
        Case{Type::Double, "DOUBLE", Type::Float, "DOUBLE", false},
        Case{Type::Float, "DOUBLE", Type::Int, "INT", false},
        Case{Type::Int, "INT", Type::Float, "DOUBLE", false},
        Case{Type::Double, "DECIMAL(10,2)", Type::Double, "NUMERIC(10,2)", false},
        Case{Type::Char, "TINYINT", Type::Char, "SMALLINT", true},
        Case{Type::UnsignedChar, "TINYINT UNSIGNED", Type::UnsignedChar, "SMALLINT UNSIGNED", true},
    };
    for (const auto& item : cases)
    {
        SCOPED_TRACE(std::string{item.sourceSql} + " -> " + std::string{item.targetSql});
        assertTypePolicy(Backend::Mysql, item.source, std::string{item.sourceSql}, item.target,
                         std::string{item.targetSql}, item.safe);
    }
}

TEST(MigrationDiffCoverageTest, Unsigned64LogicalDomainUsesTheBackendStorageLimit)
{
    assertTypePolicy(Backend::Sqlite, Type::UnsignedLongLong, "UNSIGNED BIG INT", Type::LongLong, "BIGINT", true);
    assertTypePolicy(Backend::Postgres, Type::UnsignedLongLong, "NUMERIC(20,0)", Type::LongLong, "BIGINT", false);
    assertTypePolicy(Backend::Mariadb, Type::UnsignedLongLong, "BIGINT UNSIGNED", Type::LongLong, "BIGINT", false);
    assertTypePolicy(Backend::Sqlite, Type::UnsignedLongLong, "UNSIGNED BIG INT", Type::UnsignedInt, "UNSIGNED BIG INT",
                     false);
}

TEST(MigrationDiffCoverageTest, StringWideningIsAutomaticOnlyForCanonicalPhysicalDefinitions)
{
    for (const auto backend : generationBackends)
    {
        const auto physicalString = [backend](std::size_t length)
        { return backend == Backend::Sqlite ? std::string{"TEXT"} : "VARCHAR(" + std::to_string(length) + ")"; };
        auto before = diffSnapshot(backend);
        auto& source = valueColumn(before, backend);
        source.logicalType = Type::String;
        source.sqlType = physicalString(16);
        source.maxStringLength = 16;
        auto after = before;
        auto& target = valueColumn(after, backend);
        target.sqlType = physicalString(32);
        target.maxStringLength = 32;
        const auto widened = compare(before, after);
        ASSERT_EQ(widened.changes.size(), 1U);
        EXPECT_FALSE(widened.changes.front().dataLoss);
        EXPECT_FALSE(widened.changes.front().unresolved);
        target.sqlType = backend == Backend::Sqlite ? "CLOB" : "CHARACTER VARYING(32)";
        const auto noncanonical = compare(before, after);
        ASSERT_EQ(noncanonical.changes.size(), 1U);
        EXPECT_TRUE(noncanonical.changes.front().dataLoss);
        EXPECT_TRUE(noncanonical.changes.front().unresolved);
    }
}

TEST(MigrationDiffCoverageTest, InvalidColumnRenamesProduceBlockingDiagnostics)
{
    const auto snapshot = diffSnapshot();
    for (const auto& rename :
         std::array{ColumnRename{"diff_values", "missing", "value"}, ColumnRename{"diff_values", "value", "missing"},
                    ColumnRename{"diff_values", "value", "value"}, ColumnRename{"missing_table", "value", "renamed"}})
    {
        GenerationDecisions decisions;
        decisions.columnRenames.push_back(rename);
        const auto diff = compare(snapshot, snapshot, decisions);
        EXPECT_TRUE(std::ranges::any_of(diff.diagnostics,
                                        [](const auto& item) { return item.code == "invalid-rename" && item.error; }));
    }
}

TEST(MigrationDiffCoverageTest, BackendSetChangesProduceDiagnosticsInsteadOfDroppingAProfile)
{
    const auto both = capture(migration_diff_models::Schema::view, std::array{Backend::Sqlite, Backend::Postgres});
    const auto sqlite = diffSnapshot();
    const auto removal = compare(both, sqlite);
    EXPECT_TRUE(std::ranges::any_of(removal.diagnostics, [](const auto& item)
                                    { return item.code == "backend-profile" && item.backend == Backend::Empty; }));
    EXPECT_TRUE(std::ranges::any_of(removal.diagnostics, [](const auto& item)
                                    { return item.code == "backend-profile" && item.backend == Backend::Postgres; }));
    const auto addition = compare(sqlite, both);
    EXPECT_TRUE(std::ranges::any_of(addition.diagnostics,
                                    [](const auto& item) { return item.code == "backend-profile" && item.error; }));
}
