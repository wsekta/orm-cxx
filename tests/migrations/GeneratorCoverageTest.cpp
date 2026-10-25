module;

#include <set>

#include "tests/UnitTestPrelude.hpp"

module orm;

import :internal;
import :migrations_internal;

namespace generation_coverage_models
{
struct Record
{
    int id;
    std::optional<std::string> label;
    int value;
    inline static constexpr orm::reflection::FixedString table_name{"coverage_generation_records"};
};
using Schema = orm::Schema<Record>;
}

namespace
{
using namespace orm::migrations;
using Backend = orm::db::BackendType;
using Type = orm::model::ColumnType;

auto coverageBase(Backend backend) -> SchemaSnapshot
{
    return capture(generation_coverage_models::Schema::view, std::array{backend});
}

auto coverageLiteral(Backend backend, Type type, std::string native, GenerationLiteral value,
                     std::optional<std::size_t> bound = {}, bool nullable = false) -> GeneratedMigration
{
    const auto before = coverageBase(backend);
    auto after = before;
    after.backends.at(backend).tables.front().columns.push_back({.name = "added",
                                                                 .logicalType = type,
                                                                 .sqlType = std::move(native),
                                                                 .nullable = nullable,
                                                                 .maxStringLength = bound});
    GenerationDecisions decisions;
    decisions.backfills = {{.table = "coverage_generation_records", .column = "added", .constant = std::move(value)}};
    return generate(2, "typed_literal", compare(before, after, decisions), decisions);
}

template <typename Function>
auto expectGenerationDiagnostic(Function&& operation, std::string_view code) -> void
{
    try
    {
        operation();
        FAIL() << "Expected generation failure " << code;
    }
    catch (const GenerationError& error)
    {
        ASSERT_FALSE(error.diagnostics().empty());
        EXPECT_EQ(error.diagnostics().front().code, code);
    }
}

auto coverageApprovals(const SchemaSnapshot& before, const SchemaSnapshot& after,
                       GenerationDecisions decisions = {}) -> GenerationDecisions
{
    for (const auto& change : compare(before, after, decisions).changes)
        if (change.dataLoss)
            decisions.allowDataLossUp.push_back(change.id);
    return decisions;
}
}

TEST(MigrationGeneratorCoverageTest, IntegerLiteralBoundariesMatchEveryLogicalDomain)
{
    struct Case
    {
        Type type;
        GenerationLiteral accepted;
        GenerationLiteral rejected;
        std::string expected;
    };
    const std::vector<Case> cases{
        {Type::Char, std::int64_t{std::numeric_limits<char>::lowest()},
         std::int64_t{std::numeric_limits<char>::lowest()} - 1, std::to_string(std::numeric_limits<char>::lowest())},
        {Type::UnsignedChar, std::uint64_t{255}, std::uint64_t{256}, "255"},
        {Type::Short, std::int64_t{std::numeric_limits<short>::lowest()},
         std::int64_t{std::numeric_limits<short>::lowest()} - 1, std::to_string(std::numeric_limits<short>::lowest())},
        {Type::UnsignedShort, std::uint64_t{65'535}, std::int64_t{-1}, "65535"},
        {Type::UnsignedInt, std::uint64_t{std::numeric_limits<unsigned int>::max()},
         std::uint64_t{std::numeric_limits<unsigned int>::max()} + 1,
         std::to_string(std::numeric_limits<unsigned int>::max())},
        {Type::LongLong, std::int64_t{std::numeric_limits<std::int64_t>::lowest()},
         std::uint64_t{std::numeric_limits<std::uint64_t>::max()},
         std::to_string(std::numeric_limits<std::int64_t>::lowest())},
        {Type::UnsignedLongLong, std::uint64_t{std::numeric_limits<std::int64_t>::max()}, std::int64_t{-1},
         std::to_string(std::numeric_limits<std::int64_t>::max())}};
    for (const auto& item : cases)
    {
        const auto generated = coverageLiteral(Backend::Sqlite, item.type, "BIGINT", item.accepted);
        EXPECT_NE(generated.migration.scripts.at(Backend::Sqlite).up.find(item.expected), std::string::npos);
        expectGenerationDiagnostic([&] { (void)coverageLiteral(Backend::Sqlite, item.type, "BIGINT", item.rejected); },
                                   "invalid-literal");
    }
    for (const auto backend : {Backend::Sqlite, Backend::Postgres})
        expectGenerationDiagnostic(
            [&]
            {
                (void)coverageLiteral(backend, Type::UnsignedLongLong, "BIGINT",
                                      std::uint64_t{std::numeric_limits<std::uint64_t>::max()});
            },
            "invalid-literal");
    expectGenerationDiagnostic([] { (void)coverageLiteral(Backend::Sqlite, Type::Int, "INTEGER", std::string{"12"}); },
                               "invalid-literal");
    expectGenerationDiagnostic([] { (void)coverageLiteral(Backend::Sqlite, Type::String, "TEXT", std::int64_t{12}); },
                               "invalid-literal");
}

TEST(MigrationGeneratorCoverageTest, FloatingConstantsRejectNonfiniteAndMismatchedTargets)
{
    for (const auto value : {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::max()})
        expectGenerationDiagnostic([&] { (void)coverageLiteral(Backend::Sqlite, Type::Float, "REAL", value); },
                                   "invalid-literal");
    expectGenerationDiagnostic([] { (void)coverageLiteral(Backend::Sqlite, Type::Int, "INTEGER", 1.5); },
                               "invalid-literal");
    const auto integer = coverageLiteral(Backend::Sqlite, Type::Double, "REAL", std::int64_t{42});
    EXPECT_NE(integer.migration.scripts.at(Backend::Sqlite).up.find("42"), std::string::npos);
    const auto boolean = coverageLiteral(Backend::Sqlite, Type::Double, "REAL", true);
    EXPECT_NE(boolean.migration.scripts.at(Backend::Sqlite).up.find("SELECT 1,"), std::string::npos);
}

TEST(MigrationGeneratorCoverageTest, Utf8BoundsCountCharactersAndRejectInvalidScalarEncodings)
{
    const auto twoCharacters = std::string{"\xc4\x85\xf0\x9f\x99\x82"};
    const auto accepted = coverageLiteral(Backend::Sqlite, Type::String, "TEXT", twoCharacters, 2);
    EXPECT_NE(accepted.migration.scripts.at(Backend::Sqlite).up.find("c485f09f9982"), std::string::npos);
    expectGenerationDiagnostic([&] { (void)coverageLiteral(Backend::Sqlite, Type::String, "TEXT", twoCharacters, 1); },
                               "invalid-literal");
    for (const auto& invalid : {std::string{"\xe2\x28\xa1"}, std::string{"\xe0\x80\xaf"}, std::string{"\xed\xa0\x80"},
                                std::string{"\xf4\x90\x80\x80"}, std::string{"\xf0\x80\x80\xaf"}, std::string{"\xc2"}})
        expectGenerationDiagnostic([&] { (void)coverageLiteral(Backend::Sqlite, Type::String, "TEXT", invalid); },
                                   "invalid-literal");
    expectGenerationDiagnostic(
        [] { (void)coverageLiteral(Backend::Postgres, Type::String, "TEXT", std::string{"a\0b", 3}); },
        "invalid-literal");
}

TEST(MigrationGeneratorCoverageTest, UuidConstantsUseCanonicalTextAndNativeTargets)
{
    const auto canonical = std::string{"01234567-89ab-7def-8123-456789aBCDEF"};
    const auto accepted = coverageLiteral(Backend::Postgres, Type::Uuid, "UUID", canonical);
    EXPECT_NE(accepted.migration.scripts.at(Backend::Postgres).up.find("'01234567-89ab-7def-8123-456789abcdef'::uuid"),
              std::string::npos);
    const auto maria = coverageLiteral(Backend::Mariadb, Type::Uuid, "UUID", canonical);
    EXPECT_NE(maria.migration.scripts.at(Backend::Mariadb).up.find("'01234567-89ab-7def-8123-456789abcdef'"),
              std::string::npos);
    EXPECT_EQ(maria.migration.scripts.at(Backend::Mariadb).up.find("::uuid"), std::string::npos);
    for (const auto& malformed : {std::string{"short"}, std::string{"01234567_89ab-cdef-0123-456789abcdef"},
                                  std::string{"g1234567-89ab-cdef-0123-456789abcdef"}})
        expectGenerationDiagnostic([&] { (void)coverageLiteral(Backend::Postgres, Type::Uuid, "UUID", malformed); },
                                   "invalid-literal");
    for (const auto backend : {Backend::Sqlite, Backend::Mysql})
        expectGenerationDiagnostic([&] { (void)coverageLiteral(backend, Type::Uuid, "UUID", canonical); },
                                   "invalid-snapshot");
    expectGenerationDiagnostic(
        [&] {
            (void)coverageLiteral(Backend::Mariadb, Type::Uuid, "UUID",
                                  std::string{"01234567-89ab-8def-0123-456789abcdef"});
        },
        "invalid-literal");
}

TEST(MigrationGeneratorCoverageTest, NullableBackfillsAndBackendExpressionsKeepExplicitSemantics)
{
    const auto nullable = coverageLiteral(Backend::Sqlite, Type::String, "TEXT", std::monostate{}, {}, true);
    EXPECT_NE(nullable.migration.scripts.at(Backend::Sqlite).up.find("NULL"), std::string::npos);
    const auto before = coverageBase(Backend::Mysql);
    auto after = before;
    after.backends.at(Backend::Mysql)
        .tables.front()
        .columns.push_back({.name = "added", .logicalType = Type::String, .sqlType = "LONGTEXT", .nullable = true});
    GenerationDecisions decisions;
    decisions.backfills = {{.table = "coverage_generation_records",
                            .column = "added",
                            .expressions = {{Backend::Mysql, R"sql(CONCAT('it\'s', 'a''b'))sql"}}}};
    const auto accepted = generate(2, "escaped_expression", compare(before, after, decisions), decisions);
    EXPECT_NE(accepted.migration.scripts.at(Backend::Mysql).up.find("CONCAT"), std::string::npos);
    decisions.backfills.front().expressions = {{Backend::Sqlite, "'other backend'"}};
    expectGenerationDiagnostic(
        [&] { (void)generate(2, "missing_expression", compare(before, after, decisions), decisions); },
        "missing-expression");
    decisions.backfills.front().expressions = {{Backend::Mysql, "'value'"}};
    decisions.backfills.push_back(decisions.backfills.front());
    expectGenerationDiagnostic([&]
                               { (void)generate(2, "duplicate_hint", compare(before, after, decisions), decisions); },
                               "invalid-transformation");
}

TEST(MigrationGeneratorCoverageTest, NativeIdentityConversionsRequireManualSequencePreservation)
{
    for (const auto backend : {Backend::Postgres, Backend::Mysql, Backend::Mariadb})
    {
        auto before = coverageBase(backend);
        auto& columns = before.backends.at(backend).tables.front().columns;
        std::ranges::find(columns, "id", &PhysicalColumn::name)->autoIncrement = true;
        auto after = before;
        std::ranges::find(after.backends.at(backend).tables.front().columns, "value", &PhysicalColumn::name)->nullable =
            true;
        GenerationDecisions decisions;
        decisions.conversions = {{.table = "coverage_generation_records", .column = "id", .sourceColumn = "id"}};
        expectGenerationDiagnostic(
            [&] { (void)generate(2, "identity_conversion", compare(before, after, decisions), decisions); },
            "identity-conversion");
    }
}

TEST(MigrationGeneratorCoverageTest, ReplacingPrimaryKeyWithNewIdentityMaintainsItsSupportingIndex)
{
    for (const auto backend : generationBackends)
    {
        const auto before = coverageBase(backend);
        auto after = before;
        auto& table = after.backends.at(backend).tables.front();
        auto replacement = *std::ranges::find(table.columns, "id", &PhysicalColumn::name);
        std::erase_if(table.columns, [](const auto& column) { return column.name == "id"; });
        replacement.name = "new_id";
        replacement.autoIncrement = true;
        table.columns.push_back(std::move(replacement));
        table.primaryKey = {"new_id"};
        const auto decisions = coverageApprovals(before, after);
        const auto generated = generate(2, "replace_primary", compare(before, after, decisions), decisions);
        const auto& scripts = generated.migration.scripts.at(backend);
        if (backend == Backend::Mysql || backend == Backend::Mariadb)
        {
            EXPECT_LT(scripts.up.find("ADD INDEX"), scripts.up.find("DROP PRIMARY KEY"));
            EXPECT_LT(scripts.up.find("ADD PRIMARY KEY"), scripts.up.find("DROP INDEX"));
            EXPECT_NE(scripts.up.find("AUTO_INCREMENT"), std::string::npos);
        }
        else if (backend == Backend::Postgres)
        {
            EXPECT_NE(scripts.up.find("GENERATED BY DEFAULT AS IDENTITY"), std::string::npos);
            EXPECT_NE(scripts.up.find("setval"), std::string::npos);
        }
        else
        {
            ASSERT_EQ(scripts.upContext.rebuildTables.size(), 1U);
            EXPECT_FALSE(scripts.upContext.rebuildTables.front().sourceIdentity);
            EXPECT_EQ(scripts.upContext.rebuildTables.front().targetIdentity, "new_id");
        }
    }
}

TEST(MigrationGeneratorCoverageTest, PostgresqlNullableRelaxationDropsOnlyTheRequiredConstraint)
{
    const auto before = coverageBase(Backend::Postgres);
    auto after = before;
    std::ranges::find(after.backends.at(Backend::Postgres).tables.front().columns, "value", &PhysicalColumn::name)
        ->nullable = true;
    const auto generated = generate(2, "nullable", compare(before, after));
    const auto& scripts = generated.migration.scripts.at(Backend::Postgres);
    EXPECT_NE(scripts.up.find("DROP NOT NULL"), std::string::npos);
    EXPECT_EQ(scripts.up.find("DROP COLUMN"), std::string::npos);
    EXPECT_FALSE(scripts.down);
}

TEST(MigrationGeneratorCoverageTest, BackendSpecificDeltaProducesIndependentNoopVariants)
{
    const auto before = capture(generation_coverage_models::Schema::view);
    auto after = before;
    std::ranges::find(after.backends.at(Backend::Postgres).tables.front().columns, "value", &PhysicalColumn::name)
        ->nullable = true;
    const auto generated = generate(2, "postgres_only", compare(before, after));
    for (const auto backend : {Backend::Sqlite, Backend::Mysql, Backend::Mariadb})
    {
        const auto& scripts = generated.migration.scripts.at(backend);
        EXPECT_EQ(scripts.up, "SELECT 1;\n");
        EXPECT_EQ(scripts.down, "SELECT 1;\n");
        EXPECT_EQ(scripts.beforeHash, scripts.afterHash);
        EXPECT_EQ(scripts.upContext.source, before.backends.at(backend));
        EXPECT_TRUE(scripts.upContext.tables.empty());
    }
}

TEST(MigrationGeneratorCoverageTest, NumericTypeParametersCannotOmitTheSecondParameter)
{
    PhysicalColumn column{.name = "value", .logicalType = Type::Double, .sqlType = "DECIMAL(10,2)"};
    EXPECT_NO_THROW((void)orm::migrations::detail::renderColumn(Backend::Postgres, column));
    column.sqlType = "DECIMAL(10,)";
    expectGenerationDiagnostic([&] { (void)orm::migrations::detail::renderColumn(Backend::Postgres, column); },
                               "invalid-snapshot");
}

TEST(MigrationGeneratorCoverageTest, WholeTableLossDoesNotGenerateAnEmptyPretendRestoration)
{
    for (const auto backend : generationBackends)
    {
        const auto before = coverageBase(backend);
        const auto after = emptySnapshot(std::array{backend});
        const auto decisions = coverageApprovals(before, after);
        const auto generated = generate(2, "remove_table", compare(before, after, decisions), decisions);
        const auto& scripts = generated.migration.scripts.at(backend);
        EXPECT_NE(scripts.up.find("DROP TABLE"), std::string::npos);
        EXPECT_FALSE(scripts.down);
        if (backend == Backend::Sqlite)
        {
            EXPECT_EQ(scripts.upContext.mode, MigrationExecutionMode::SqliteRebuild);
            EXPECT_EQ(scripts.upContext.tables, std::vector<std::string>{"coverage_generation_records"});
            EXPECT_TRUE(scripts.upContext.rebuildTables.empty());
        }
    }
}

TEST(MigrationGeneratorCoverageTest, ColonBearingRenamePairsHaveIndependentStagingNames)
{
    for (const auto backend : generationBackends)
    {
        auto before = coverageBase(backend);
        auto& tables = before.backends.at(backend).tables;
        auto first = tables.front();
        first.name = "a:b";
        first.primaryKeyName = backend == Backend::Mysql || backend == Backend::Mariadb ? "PRIMARY" : "pk_first";
        auto second = first;
        second.name = "a";
        second.primaryKeyName = backend == Backend::Mysql || backend == Backend::Mariadb ? "PRIMARY" : "pk_second";
        tables = {first, second};
        auto after = before;
        for (auto& table : after.backends.at(backend).tables)
            table.name = table.name == "a:b" ? "c" : "b:c";
        GenerationDecisions decisions;
        decisions.tableRenames = {{"a:b", "c"}, {"a", "b:c"}};
        const auto generated = generate(2, "framed_names", compare(before, after, decisions), decisions);
        const auto& scripts = generated.migration.scripts.at(backend);
        ASSERT_TRUE(scripts.down);
        EXPECT_NO_THROW((Catalog{generated.migration}));
        if (backend == Backend::Sqlite)
        {
            ASSERT_EQ(scripts.upContext.rebuildTables.size(), 2U);
            EXPECT_NE(scripts.upContext.rebuildTables[0].staging, scripts.upContext.rebuildTables[1].staging);
        }
        else
        {
            const auto stageFrom = [&](std::string_view source)
            {
                const auto prefix =
                    "ALTER TABLE " + orm::migrations::detail::quoteIdentifier(backend, source) + " RENAME TO ";
                const auto begin = scripts.up.find(prefix);
                EXPECT_NE(begin, std::string::npos);
                if (begin == std::string::npos)
                    return std::string{};
                const auto start = begin + prefix.size();
                return scripts.up.substr(start, scripts.up.find(';', start) - start);
            };
            EXPECT_NE(stageFrom("a:b"), stageFrom("a"));
        }
    }
}

TEST(MigrationGeneratorCoverageTest, SlashAndPercentBearingNamesNeedIndependentRiskApprovals)
{
    const auto backend = Backend::Sqlite;
    auto before = coverageBase(backend);
    auto& tables = before.backends.at(backend).tables;
    const auto prototype = tables.front();
    tables.clear();
    for (const auto& [tableName, columnName] :
         std::vector<std::pair<std::string, std::string>>{{"a/b", "c"}, {"a", "b/c"}, {"a%2Fb", "c"}})
    {
        auto table = prototype;
        table.name = tableName;
        std::ranges::find(table.columns, "value", &PhysicalColumn::name)->name = columnName;
        tables.push_back(std::move(table));
    }
    auto after = before;
    for (auto& table : after.backends.at(backend).tables)
        std::erase_if(table.columns, [](const auto& column) { return column.name != "id" && column.name != "label"; });
    const auto diff = compare(before, after);
    ASSERT_EQ(diff.changes.size(), 3U);
    std::set<std::string> ids;
    for (const auto& change : diff.changes)
        ids.insert(change.id);
    EXPECT_EQ(ids, (std::set<std::string>{"sqlite/drop-column/a%2Fb/c", "sqlite/drop-column/a/b%2Fc",
                                          "sqlite/drop-column/a%252Fb/c"}));
    GenerationDecisions one;
    one.allowDataLossUp = {"sqlite/drop-column/a%2Fb/c"};
    const auto partial = compare(before, after, one);
    EXPECT_EQ(std::ranges::count_if(partial.changes, [](const auto& change) { return change.unresolved; }), 2);
    EXPECT_THROW((void)generate(2, "partial_approval", partial, one), GenerationError);
    const auto all = coverageApprovals(before, after);
    EXPECT_NO_THROW((void)generate(2, "independent_approvals", compare(before, after, all), all));
}
