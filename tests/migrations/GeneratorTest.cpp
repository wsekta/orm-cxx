module;

#include "tests/UnitTestPrelude.hpp"

module orm;

import :internal;
import :migrations_internal;

namespace generation_test_models
{
struct Before
{
    int id;
    std::optional<std::string> label;
    short value;
    inline static constexpr orm::reflection::FixedString table_name{"generator_records"};
};
struct After
{
    int id;
    std::string title;
    int value;
    std::optional<std::string> note;
    inline static constexpr orm::reflection::FixedString table_name{"generator_accounts"};
};
using Old = orm::Schema<Before>;
using New = orm::Schema<After>;
}

namespace
{
using namespace orm::migrations;
using Backend = orm::db::BackendType;

auto oldSnapshot() -> SchemaSnapshot
{
    return capture(generation_test_models::Old::view, std::array{Backend::Sqlite});
}
auto newSnapshot() -> SchemaSnapshot
{
    return capture(generation_test_models::New::view, std::array{Backend::Sqlite});
}
auto renamesAndFill() -> GenerationDecisions
{
    GenerationDecisions decisions;
    decisions.tableRenames = {{"generator_records", "generator_accounts"}};
    decisions.columnRenames = {{"generator_accounts", "label", "title"}};
    decisions.backfills = {{.table = "generator_accounts", .column = "title", .constant = std::string{"unknown"}}};
    return decisions;
}
auto approveUp(const SchemaSnapshot& before, const SchemaSnapshot& after,
               GenerationDecisions decisions = {}) -> GenerationDecisions
{
    for (const auto& change : compare(before, after, decisions).changes)
        if (change.dataLoss)
            decisions.allowDataLossUp.push_back(change.id);
    return decisions;
}
}

TEST(MigrationGeneratorTest, InitialMigrationIncludesOwnedSnapshotsAndRequiresDownApproval)
{
    const auto after = oldSnapshot();
    const auto before = emptySnapshot(std::array{Backend::Sqlite});
    const auto generated = generate(1, "initial", compare(before, after));
    const auto& script = generated.migration.scripts.at(Backend::Sqlite);
    EXPECT_NE(script.up.find("CREATE TABLE"), std::string::npos);
    EXPECT_FALSE(script.down);
    EXPECT_EQ(generated.migration.before, before);
    EXPECT_EQ(generated.migration.after, after);
    EXPECT_EQ(script.upContext.source, before.backends.at(Backend::Sqlite));
    GenerationDecisions decisions;
    decisions.allowDataLossDown = {"sqlite/drop-table/generator_records"};
    const auto reversible = generate(1, "initial", compare(before, after), decisions);
    ASSERT_TRUE(reversible.migration.scripts.at(Backend::Sqlite).down);
    EXPECT_NE(reversible.migration.scripts.at(Backend::Sqlite).down->find("DROP TABLE"), std::string::npos);
}

TEST(MigrationGeneratorTest, RenamesAreExplicitAndRequiredBackfillIsAppliedDuringRebuild)
{
    const auto before = oldSnapshot();
    const auto after = newSnapshot();
    EXPECT_THROW((void)generate(2, "rename", compare(before, after)), GenerationError);
    const auto decisions = renamesAndFill();
    const auto generated = generate(2, "rename", compare(before, after, decisions), decisions);
    const auto& scripts = generated.migration.scripts.at(Backend::Sqlite);
    ASSERT_EQ(scripts.upContext.mode, MigrationExecutionMode::SqliteRebuild);
    ASSERT_EQ(scripts.upContext.rebuildTables.size(), 1);
    const auto& rebuild = scripts.upContext.rebuildTables.front();
    EXPECT_EQ(rebuild.source, "generator_records");
    EXPECT_EQ(rebuild.target, "generator_accounts");
    EXPECT_TRUE(rebuild.staging.starts_with("__orm_rebuild_"));
    EXPECT_NE(scripts.up.find("COALESCE(\"label\""), std::string::npos);
    EXPECT_LT(scripts.up.find("INSERT INTO"), scripts.up.find("DROP TABLE"));
    EXPECT_LT(scripts.up.find("DROP TABLE"), scripts.up.find("RENAME TO"));
    EXPECT_FALSE(scripts.down); // Removing the newly added note needs its own down approval.
}

TEST(MigrationGeneratorTest, LogicalNarrowingCannotHideBehindEqualNativeSqlTypes)
{
    auto before = oldSnapshot();
    auto after = before;
    auto& column = after.backends.at(Backend::Sqlite).tables.front().columns;
    const auto found = std::ranges::find(column, "value", &PhysicalColumn::name);
    ASSERT_NE(found, column.end());
    found->logicalType = orm::model::ColumnType::Char;
    const auto diff = compare(before, after);
    const auto change = std::ranges::find_if(diff.changes, [](const auto& value) { return value.column == "value"; });
    ASSERT_NE(change, diff.changes.end());
    EXPECT_TRUE(change->dataLoss);
    EXPECT_TRUE(change->unresolved);
    EXPECT_THROW((void)generate(2, "narrow", diff), GenerationError);
    auto decisions = approveUp(before, after);
    EXPECT_THROW((void)generate(2, "narrow", diff, decisions), GenerationError);
    decisions.conversions = {{.table = "generator_records", .column = "value", .constant = std::int64_t{7}}};
    EXPECT_NO_THROW((void)generate(2, "narrow", diff, decisions));
}

TEST(MigrationGeneratorTest, LiteralBackfillsValidateLogicalRangesAndNullability)
{
    auto decisions = renamesAndFill();
    decisions.backfills.front().constant = GenerationLiteral{std::monostate{}};
    EXPECT_THROW((void)generate(2, "bad_null", compare(oldSnapshot(), newSnapshot(), decisions), decisions),
                 GenerationError);
    decisions = renamesAndFill();
    decisions.conversions = {{.table = "generator_accounts", .column = "value", .constant = std::uint64_t{1ULL << 40}}};
    EXPECT_THROW((void)generate(2, "overflow", compare(oldSnapshot(), newSnapshot(), decisions), decisions),
                 GenerationError);
}

TEST(MigrationGeneratorTest, TransformationsRejectAmbiguousSourcesAndSqlCommands)
{
    auto decisions = renamesAndFill();
    decisions.backfills.front().sourceColumn = "label";
    EXPECT_THROW((void)generate(2, "ambiguous", compare(oldSnapshot(), newSnapshot(), decisions), decisions),
                 GenerationError);
    decisions = renamesAndFill();
    decisions.backfills.front().constant.reset();
    decisions.backfills.front().expressions = {{Backend::Sqlite, "'x'; DROP TABLE generator_records"}};
    EXPECT_THROW((void)generate(2, "commands", compare(oldSnapshot(), newSnapshot(), decisions), decisions),
                 GenerationError);
    decisions.backfills.front().expressions = {{Backend::Sqlite, "upper('a;b')"}};
    EXPECT_NO_THROW((void)generate(2, "expression", compare(oldSnapshot(), newSnapshot(), decisions), decisions));
}

TEST(MigrationGeneratorTest, DuplicateRenamesUnknownApprovalsAndEmptyDiffAreRejected)
{
    auto decisions = renamesAndFill();
    decisions.tableRenames.push_back(decisions.tableRenames.front());
    EXPECT_FALSE(compare(oldSnapshot(), newSnapshot(), decisions).diagnostics.empty());
    EXPECT_THROW((void)generate(2, "duplicate", compare(oldSnapshot(), newSnapshot(), decisions), decisions),
                 GenerationError);
    decisions = renamesAndFill();
    decisions.allowDataLossUp = {"sqlite/drop-table/misspelled"};
    EXPECT_THROW((void)generate(2, "typo", compare(oldSnapshot(), newSnapshot(), decisions), decisions),
                 GenerationError);
    EXPECT_THROW((void)generate(2, "empty", compare(oldSnapshot(), oldSnapshot())), GenerationError);
    EXPECT_THROW((void)generate(0, "initial", compare(emptySnapshot(std::array{Backend::Sqlite}), oldSnapshot())),
                 GenerationError);
}

TEST(MigrationGeneratorTest, DropColumnIsExplicitAndHasNoPretendDataRestoration)
{
    const auto before = oldSnapshot();
    auto after = before;
    auto& columns = after.backends.at(Backend::Sqlite).tables.front().columns;
    std::erase_if(columns, [](const auto& column) { return column.name == "label"; });
    EXPECT_THROW((void)generate(2, "drop", compare(before, after)), GenerationError);
    const auto decisions = approveUp(before, after);
    const auto generated = generate(2, "drop", compare(before, after, decisions), decisions);
    EXPECT_FALSE(generated.migration.scripts.at(Backend::Sqlite).down);
}

TEST(MigrationGeneratorTest, GeneratedSqlAndContextsAreDeterministic)
{
    const auto decisions = renamesAndFill();
    const auto diff = compare(oldSnapshot(), newSnapshot(), decisions);
    const auto first = generate(20, "deterministic", diff, decisions);
    const auto second = generate(20, "deterministic", diff, decisions);
    const auto& one = first.migration.scripts.at(Backend::Sqlite);
    const auto& two = second.migration.scripts.at(Backend::Sqlite);
    EXPECT_EQ(one.up, two.up);
    EXPECT_EQ(one.upContext, two.upContext);
    EXPECT_EQ(one.beforeHash, two.beforeHash);
    EXPECT_EQ(one.afterHash, two.afterHash);
}

TEST(MigrationGeneratorTest, ExplicitInverseBackfillAllowsStructuralRestorationOfDroppedColumn)
{
    const auto before = oldSnapshot();
    auto after = before;
    std::erase_if(after.backends.at(Backend::Sqlite).tables.front().columns,
                  [](const auto& column) { return column.name == "label"; });
    auto decisions = approveUp(before, after);
    decisions.downBackfills = {
        {.table = "generator_records", .column = "label", .constant = std::string{"replacement"}}};
    const auto generated = generate(2, "restore_substitute", compare(before, after, decisions), decisions);
    const auto& script = generated.migration.scripts.at(Backend::Sqlite);
    ASSERT_TRUE(script.down);
    EXPECT_EQ(script.downContext.source, after.backends.at(Backend::Sqlite));
    EXPECT_EQ(script.downContext.target, before.backends.at(Backend::Sqlite));
    EXPECT_NE(script.down->find("CAST(X'"), std::string::npos);
}

TEST(MigrationGeneratorTest, NativeVariantsContainSnapshotPreflightAndFullColumnDefinitions)
{
    for (const auto backend : {Backend::Postgres, Backend::Mysql, Backend::Mariadb})
    {
        const auto before = capture(generation_test_models::Old::view, std::array{backend});
        const auto after = capture(generation_test_models::New::view, std::array{backend});
        const auto decisions = renamesAndFill();
        const auto generated = generate(2, "native", compare(before, after, decisions), decisions);
        const auto& script = generated.migration.scripts.at(backend);
        EXPECT_EQ(script.upContext.source, before.backends.at(backend));
        EXPECT_EQ(script.upContext.target, after.backends.at(backend));
        EXPECT_EQ(script.upContext.tables, std::vector<std::string>{"generator_records"});
        EXPECT_NE(script.up.find("RENAME COLUMN"), std::string::npos);
        if (backend != Backend::Postgres)
        {
            EXPECT_NE(script.up.find("MODIFY COLUMN"), std::string::npos);
            EXPECT_NE(script.up.find("NOT NULL"), std::string::npos);
        }
    }
}

TEST(MigrationGeneratorTest, BackfillStringLiteralsAreEncodedWithoutSqlOrConnectionModeAmbiguity)
{
    for (const auto backend : generationBackends)
    {
        const auto before = capture(generation_test_models::Old::view, std::array{backend});
        auto after = before;
        after.backends.at(backend).tables.front().columns.push_back(
            {.name = "extra",
             .logicalType = orm::model::ColumnType::String,
             .sqlType = backend == Backend::Mysql || backend == Backend::Mariadb ? "LONGTEXT" : "TEXT",
             .nullable = false});
        GenerationDecisions decisions;
        decisions.backfills = {{.table = "generator_records", .column = "extra", .constant = std::string{"'\\;\n"}}};
        const auto generated = generate(2, "encoded", compare(before, after, decisions), decisions);
        const auto& sql = generated.migration.scripts.at(backend).up;
        EXPECT_EQ(sql.find("'\\;\n"), std::string::npos);
        EXPECT_NE(sql.find("275c3b0a"), std::string::npos);
    }
}

TEST(MigrationGeneratorTest, SourceHintsRequireExistingNonNullableSourcesForRequiredTargets)
{
    auto decisions = renamesAndFill();
    decisions.backfills.front().constant.reset();
    decisions.backfills.front().sourceColumn = "missing";
    EXPECT_THROW((void)generate(2, "missing_source", compare(oldSnapshot(), newSnapshot(), decisions), decisions),
                 GenerationError);
    decisions.backfills.front().sourceColumn = "label";
    EXPECT_THROW((void)generate(2, "nullable_source", compare(oldSnapshot(), newSnapshot(), decisions), decisions),
                 GenerationError);
    decisions = renamesAndFill();
    decisions.conversions = {{.table = "generator_accounts", .column = "value", .sourceColumn = "id"}};
    EXPECT_NO_THROW((void)generate(2, "source", compare(oldSnapshot(), newSnapshot(), decisions), decisions));
}

TEST(MigrationGeneratorTest, MalformedUtf8AndIncompleteExpressionsAreRejectedBeforeWritingSql)
{
    auto decisions = renamesAndFill();
    decisions.backfills.front().constant = std::string{1, static_cast<char>(0xff)};
    EXPECT_THROW((void)generate(2, "utf8", compare(oldSnapshot(), newSnapshot(), decisions), decisions),
                 GenerationError);
    for (const auto& expression : {"", "coalesce('x',", ")'x'", "'unclosed", "1 /* comment */", "$a$x$a$"})
    {
        decisions = renamesAndFill();
        decisions.backfills.front().constant.reset();
        decisions.backfills.front().expressions = {{Backend::Sqlite, expression}};
        EXPECT_THROW(
            (void)generate(2, "invalid_expression", compare(oldSnapshot(), newSnapshot(), decisions), decisions),
            GenerationError);
    }
}

TEST(MigrationGeneratorTest, BackendProfileChangesAreReportedInsteadOfGuessingNewDdl)
{
    const auto before = oldSnapshot();
    auto after = before;
    after.backends.at(Backend::Sqlite).profile = "another-profile";
    EXPECT_THROW((void)compare(before, after), GenerationError);
    after = emptySnapshot(std::array{Backend::Postgres});
    EXPECT_FALSE(compare(before, after).diagnostics.empty());
}

TEST(MigrationGeneratorTest, UnsignedTransportCeilingIsIncludedInNativeWideningDecision)
{
    for (const auto backend : {Backend::Sqlite, Backend::Postgres})
    {
        auto before = capture(generation_test_models::Old::view, std::array{backend});
        auto& columns = before.backends.at(backend).tables.front().columns;
        auto& column = *std::ranges::find(columns, "value", &PhysicalColumn::name);
        column.logicalType = orm::model::ColumnType::UnsignedLongLong;
        column.sqlType = "BIGINT";
        auto after = before;
        auto& target =
            *std::ranges::find(after.backends.at(backend).tables.front().columns, "value", &PhysicalColumn::name);
        target.logicalType = orm::model::ColumnType::LongLong;
        const auto widened = compare(before, after);
        ASSERT_EQ(widened.changes.size(), 1);
        EXPECT_FALSE(widened.changes.front().dataLoss);
        EXPECT_FALSE(widened.changes.front().unresolved);
        const auto inverse = compare(after, before);
        ASSERT_EQ(inverse.changes.size(), 1);
        EXPECT_TRUE(inverse.changes.front().dataLoss);
        EXPECT_TRUE(inverse.changes.front().unresolved);
    }
}

TEST(MigrationGeneratorTest, TypedFloatLiteralsAreStoredAsExactlyRepresentableFloatValues)
{
    const auto before = oldSnapshot();
    auto after = before;
    after.backends.at(Backend::Sqlite)
        .tables.front()
        .columns.push_back(
            {.name = "fraction", .logicalType = orm::model::ColumnType::Float, .sqlType = "REAL", .nullable = false});
    GenerationDecisions decisions;
    decisions.backfills = {{.table = "generator_records", .column = "fraction", .constant = 0.1}};
    const auto fraction = generate(2, "float_fraction", compare(before, after, decisions), decisions);
    EXPECT_NE(fraction.migration.scripts.at(Backend::Sqlite).up.find("0.10000000149011612"), std::string::npos);
    decisions.backfills.front().constant = std::int64_t{16'777'217};
    const auto integer = generate(2, "float_integer", compare(before, after, decisions), decisions);
    EXPECT_NE(integer.migration.scripts.at(Backend::Sqlite).up.find("16777216"), std::string::npos);
    EXPECT_EQ(integer.migration.scripts.at(Backend::Sqlite).up.find("16777217"), std::string::npos);
}

TEST(MigrationGeneratorTest, DownApprovalAppliesToItsBackendVariantIndependently)
{
    const auto before = capture(generation_test_models::Old::view);
    auto after = before;
    for (auto& [backend, schema] : after.backends)
        schema.tables.front().columns.push_back(
            {.name = "extra",
             .logicalType = orm::model::ColumnType::String,
             .sqlType = backend == Backend::Mysql || backend == Backend::Mariadb ? "LONGTEXT" : "TEXT",
             .nullable = true});
    GenerationDecisions decisions;
    decisions.allowDataLossDown = {"postgresql/drop-column/generator_records/extra"};
    const auto generated = generate(2, "backend_down", compare(before, after), decisions);
    EXPECT_TRUE(generated.migration.scripts.at(Backend::Postgres).down);
    EXPECT_FALSE(generated.migration.scripts.at(Backend::Sqlite).down);
    EXPECT_FALSE(generated.migration.scripts.at(Backend::Mysql).down);
    EXPECT_FALSE(generated.migration.scripts.at(Backend::Mariadb).down);
}

TEST(MigrationGeneratorTest, IncomingForeignKeyClosureProtectsReferencedUniqueIndexes)
{
    auto before = capture(generation_test_models::Old::view, std::array{Backend::Postgres});
    auto& tables = before.backends.at(Backend::Postgres).tables;
    auto first = tables.front();
    first.name = "chain_a";
    first.primaryKeyName = "chain_a_primary";
    auto second = first;
    second.name = "chain_b";
    second.primaryKeyName = "chain_b_primary";
    second.columns.push_back(
        {.name = "link", .logicalType = orm::model::ColumnType::Int, .sqlType = "INTEGER", .nullable = true});
    second.indexes.push_back({.name = "chain_b_unique", .columns = {"value"}, .unique = true});
    second.foreignKeys.push_back(
        {.name = "chain_b_a", .columns = {"link"}, .targetTable = "chain_a", .targetColumns = {"id"}});
    auto third = first;
    third.name = "chain_c";
    third.primaryKeyName = "chain_c_primary";
    third.columns.push_back(
        {.name = "link", .logicalType = orm::model::ColumnType::Short, .sqlType = "SMALLINT", .nullable = true});
    third.foreignKeys.push_back(
        {.name = "chain_c_b", .columns = {"link"}, .targetTable = "chain_b", .targetColumns = {"value"}});
    tables = {first, second, third};
    auto after = before;
    after.backends.at(Backend::Postgres)
        .tables.front()
        .columns.push_back(
            {.name = "extra", .logicalType = orm::model::ColumnType::String, .sqlType = "TEXT", .nullable = true});
    const auto generated = generate(2, "incoming_chain", compare(before, after));
    const auto& script = generated.migration.scripts.at(Backend::Postgres);
    EXPECT_EQ(script.upContext.tables, (std::vector<std::string>{"chain_a", "chain_b", "chain_c"}));
    const auto dropChild = script.up.find("DROP CONSTRAINT \"chain_c_b\"");
    const auto dropUnique = script.up.find("DROP INDEX \"chain_b_unique\"");
    const auto createUnique = script.up.find("CREATE UNIQUE INDEX \"chain_b_unique\"");
    const auto restoreChild = script.up.find("ADD CONSTRAINT \"chain_c_b\"");
    ASSERT_NE(dropChild, std::string::npos);
    ASSERT_NE(dropUnique, std::string::npos);
    ASSERT_NE(createUnique, std::string::npos);
    ASSERT_NE(restoreChild, std::string::npos);
    EXPECT_LT(dropChild, dropUnique);
    EXPECT_LT(createUnique, restoreChild);
}

TEST(MigrationGeneratorTest, RenameFreesOriginalNameForAnIndependentNewTable)
{
    const auto before = capture(generation_test_models::Old::view);
    auto after = before;
    for (auto& [backend, schema] : after.backends)
    {
        auto moved = schema.tables.front();
        moved.name = "generator_accounts";
        if (backend != Backend::Mysql && backend != Backend::Mariadb)
            moved.primaryKeyName = "generator_accounts_primary";
        schema.tables.push_back(std::move(moved));
    }
    GenerationDecisions decisions;
    decisions.tableRenames = {{"generator_records", "generator_accounts"}};
    const auto generated = generate(2, "reuse_original_name", compare(before, after, decisions), decisions);
    for (const auto& [backend, scripts] : generated.migration.scripts)
    {
        EXPECT_NE(
            scripts.up.find("CREATE TABLE " + orm::migrations::detail::quoteIdentifier(backend, "generator_records")),
            std::string::npos);
        EXPECT_EQ(scripts.upContext.source, before.backends.at(backend));
        EXPECT_EQ(scripts.upContext.target, generated.migration.after.backends.at(backend));
    }
}

TEST(MigrationGeneratorTest, GeneratedTableCharacterSetDoesNotBecomeSessionSet)
{
    for (const auto backend : {Backend::Mysql, Backend::Mariadb})
    {
        EXPECT_NO_THROW(orm::migrations::detail::validateScript(
            "CREATE TABLE t(id INT) ENGINE=InnoDB DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_bin; "
            "ALTER TABLE t CONVERT TO CHARACTER SET utf8mb4; UPDATE t SET id=1;",
            backend, 1));
        for (const auto script :
             {"SET CHARACTER SET utf8mb4;",
              "CREATE TABLE t(id INT) DEFAULT CHARACTER SET utf8mb4; SET CHARACTER SET latin1;",
              "CREATE PROCEDURE p() BEGIN CREATE TABLE t(id INT) DEFAULT CHARACTER SET utf8mb4; "
              "SET CHARACTER SET latin1; END;",
              "CREATE TABLE t(id INT) DEFAULT CHARACTER /*!80000 SET */ utf8mb4; /*! SET sql_mode='' */;"})
            EXPECT_THROW(orm::migrations::detail::validateScript(script, backend, 1), MigrationError);
        EXPECT_NO_THROW(orm::migrations::detail::validateScript(
            "CREATE TABLE t(id INT) DEFAULT CHARACTER /*!80000 SET */ utf8mb4;", backend, 1));
    }
}

TEST(MigrationGeneratorTest, EmbeddedCatalogRequiresContinuousAdjacentSnapshots)
{
    for (const auto backend : generationBackends)
    {
        const auto selected = std::array{backend};
        const auto empty = emptySnapshot(selected);
        const auto before = capture(generation_test_models::Old::view, selected);
        auto after = before;
        auto& columns = after.backends.at(backend).tables.front().columns;
        auto extra = *std::ranges::find(columns, "label", &PhysicalColumn::name);
        extra.name = "extra";
        columns.push_back(std::move(extra));
        const auto first = generate(1, "initial", compare(empty, before)).migration;
        const auto second = generate(2, "extra", compare(before, after)).migration;
        EXPECT_NO_THROW((Catalog{second, first}));
        const auto disconnected = generate(2, "disconnected", compare(empty, after)).migration;
        EXPECT_THROW((Catalog{first, disconnected}), MigrationError);

        Migration manual;
        manual.version = 2;
        manual.name = "manual";
        SqlMigration script;
        script.up = "SELECT 1;";
        manual.scripts.emplace(backend, std::move(script));
        auto afterManual = disconnected;
        afterManual.version = 3;
        EXPECT_NO_THROW((Catalog{first, manual, afterManual}));
    }
}

TEST(MigrationGeneratorTest, EmbeddedCatalogRejectsUnknownExecutionModesInBothDirections)
{
    const auto before = emptySnapshot(std::array{Backend::Sqlite});
    const auto after = oldSnapshot();
    const auto valid = generate(1, "initial", compare(before, after)).migration;
    using ExecutionModeValue = std::underlying_type_t<MigrationExecutionMode>;
    constexpr ExecutionModeValue unknownValue = 99;
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    const auto unknownMode = static_cast<MigrationExecutionMode>(unknownValue);
    for (const auto direction : {Direction::Up, Direction::Down})
    {
        auto invalid = valid;
        auto& script = invalid.scripts.at(Backend::Sqlite);
        auto& context = direction == Direction::Up ? script.upContext : script.downContext;
        context.mode = unknownMode;
        EXPECT_THROW((Catalog{invalid}), MigrationError);
    }
}

TEST(MigrationGeneratorTest, StringNativeChangesRequireAProvenCapacityWidening)
{
    for (const auto backend : generationBackends)
    {
        const auto before = capture(generation_test_models::Old::view, std::array{backend});
        for (const auto& native : {"VARCHAR(3)", "INTEGER"})
        {
            auto after = before;
            auto& label =
                *std::ranges::find(after.backends.at(backend).tables.front().columns, "label", &PhysicalColumn::name);
            label.sqlType = native;
            const auto diff = compare(before, after);
            ASSERT_EQ(diff.changes.size(), 1U);
            EXPECT_TRUE(diff.changes.front().dataLoss);
            EXPECT_TRUE(diff.changes.front().unresolved);
            const auto approved = approveUp(before, after);
            EXPECT_THROW((void)generate(2, "unknown_string_native", compare(before, after, approved), approved),
                         GenerationError);
            auto explicitConversion = approved;
            explicitConversion.conversions = {
                {.table = "generator_records", .column = "label", .constant = std::string{"1"}}};
            EXPECT_NO_THROW((void)generate(2, "explicit_string_native", compare(before, after, explicitConversion),
                                           explicitConversion));
        }
        auto bounded = before;
        auto& source =
            *std::ranges::find(bounded.backends.at(backend).tables.front().columns, "label", &PhysicalColumn::name);
        source.maxStringLength = 24;
        source.sqlType = backend == Backend::Sqlite ? "TEXT" : "VARCHAR(24)";
        auto widened = bounded;
        auto& target =
            *std::ranges::find(widened.backends.at(backend).tables.front().columns, "label", &PhysicalColumn::name);
        target.maxStringLength = 48;
        target.sqlType = backend == Backend::Sqlite ? "TEXT" : "VARCHAR(48)";
        const auto safe = compare(bounded, widened);
        ASSERT_EQ(safe.changes.size(), 1U);
        EXPECT_FALSE(safe.changes.front().dataLoss);
        EXPECT_FALSE(safe.changes.front().unresolved);
        EXPECT_NO_THROW((void)generate(2, "wider_string_native", safe));
    }
}

TEST(MigrationGeneratorTest, NewColumnUsingARenamedSourceNameIsNotAnExistingConversionTarget)
{
    for (const auto backend : generationBackends)
    {
        const auto before = capture(generation_test_models::Old::view, std::array{backend});
        auto after = before;
        auto& columns = after.backends.at(backend).tables.front().columns;
        auto& original = *std::ranges::find(columns, "value", &PhysicalColumn::name);
        auto added = original;
        original.name = "renamed";
        columns.push_back(std::move(added));
        GenerationDecisions decisions;
        decisions.columnRenames = {{"generator_records", "value", "renamed"}};
        decisions.backfills = {{.table = "generator_records", .column = "value", .constant = std::int64_t{91}}};
        EXPECT_NO_THROW((void)generate(2, "reuse_column_name", compare(before, after, decisions), decisions));
        decisions.conversions = {{.table = "generator_records", .column = "value", .sourceColumn = "value"}};
        EXPECT_THROW((void)generate(2, "new_column_conversion", compare(before, after, decisions), decisions),
                     GenerationError);
    }
}
