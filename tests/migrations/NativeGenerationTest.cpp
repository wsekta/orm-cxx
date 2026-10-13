module;

#include "tests/UnitTestPrelude.hpp"

module orm;

import :internal;
import :test_support;
import :migrations_internal;

namespace native_generation_models
{
struct OldGroup
{
    short first;
    int second;
    inline static constexpr orm::reflection::FixedString table_name{"generation_groups"};
    inline static constexpr auto id_columns = orm::primaryKey<&OldGroup::first, &OldGroup::second>();
};
struct OldAccount
{
    int id;
    std::optional<OldGroup> group;
    std::optional<std::string> label;
    short score;
    inline static constexpr orm::reflection::FixedString table_name{"generation_records"};
    inline static constexpr auto string_lengths = orm::stringLengths(orm::stringLength<&OldAccount::label, 24>());
};
struct NewGroup
{
    short first;
    int second;
    inline static constexpr orm::reflection::FixedString table_name{"generation_teams"};
    inline static constexpr auto id_columns = orm::primaryKey<&NewGroup::second, &NewGroup::first>();
};
struct NewAccount
{
    int id;
    std::optional<NewGroup> group;
    std::string title;
    int score;
    bool enabled;
    inline static constexpr orm::reflection::FixedString table_name{"generation_accounts"};
    inline static constexpr auto string_lengths = orm::stringLengths(orm::stringLength<&NewAccount::title, 48>());
};
struct FreshGroup
{
    short first;
    int second;
    inline static constexpr orm::reflection::FixedString table_name{"fresh_generation_teams"};
    inline static constexpr auto id_columns = orm::primaryKey<&FreshGroup::second, &FreshGroup::first>();
};
struct FreshAccount
{
    int id;
    std::optional<FreshGroup> group;
    std::string title;
    int score;
    bool enabled;
    inline static constexpr orm::reflection::FixedString table_name{"fresh_generation_accounts"};
    inline static constexpr auto string_lengths = orm::stringLengths(orm::stringLength<&FreshAccount::title, 48>());
};
struct PlainIdentity
{
    int id;
    std::string label;
    inline static constexpr orm::reflection::FixedString table_name{"generation_identity"};
};
struct AutoIdentity
{
    int id;
    std::string label;
    inline static constexpr orm::reflection::FixedString table_name{"generation_identity"};
    inline static constexpr auto auto_increment_columns = orm::autoIncrement<&AutoIdentity::id>();
};
struct RenamedIdentity
{
    int id;
    std::string caption;
    inline static constexpr orm::reflection::FixedString table_name{"generation_identity"};
};
struct ExpressionSource
{
    int id;
    int value;
    inline static constexpr orm::reflection::FixedString table_name{"expression_a_source"};
};
struct ExpressionTarget
{
    int id;
    inline static constexpr orm::reflection::FixedString table_name{"expression_z_target"};
};
struct FilledExpressionTarget
{
    int id;
    int copied;
    inline static constexpr orm::reflection::FixedString table_name{"expression_z_target"};
};
struct QualifiedExpressionBefore
{
    int id;
    int value;
    inline static constexpr orm::reflection::FixedString table_name{"expression_before"};
};
struct QualifiedExpressionAfter
{
    int id;
    int result;
    inline static constexpr orm::reflection::FixedString table_name{"expression_after"};
};
struct ReusedColumnBefore
{
    int id;
    int a;
    inline static constexpr orm::reflection::FixedString table_name{"generation_column_reuse"};
};
struct ReusedColumnAfter
{
    int id;
    int b;
    int a;
    inline static constexpr orm::reflection::FixedString table_name{"generation_column_reuse"};
};
using Before = orm::Schema<OldAccount, OldGroup>;
using After = orm::Schema<NewAccount, NewGroup>;
using Fresh = orm::Schema<FreshAccount, FreshGroup>;
using Plain = orm::Schema<PlainIdentity>;
using Identity = orm::Schema<AutoIdentity>;
using Renamed = orm::Schema<RenamedIdentity>;
using CrossExpressionBefore = orm::Schema<ExpressionSource, ExpressionTarget>;
using CrossExpressionAfter = orm::Schema<FilledExpressionTarget>;
using QualifiedBefore = orm::Schema<QualifiedExpressionBefore>;
using QualifiedAfter = orm::Schema<QualifiedExpressionAfter>;
using ReusedBefore = orm::Schema<ReusedColumnBefore>;
using ReusedAfter = orm::Schema<ReusedColumnAfter>;
} // namespace native_generation_models

namespace
{
using namespace orm::migrations;
using Backend = orm::db::BackendType;
namespace migration_detail = orm::migrations::detail;
namespace generated_models = native_generation_models;

auto approveChanges(const SchemaSnapshot& before, const SchemaSnapshot& after,
                    GenerationDecisions decisions = {}) -> GenerationDecisions
{
    const auto diff = compare(before, after, decisions);
    for (const auto& change : diff.changes)
    {
        if (change.dataLoss)
            decisions.allowDataLossUp.push_back(change.id);
    }
    return decisions;
}

struct ActualTableShape
{
    std::vector<std::string> columns;
    std::vector<std::string> primaryKey;
    std::vector<std::string> foreignKeys;
    auto operator==(const ActualTableShape&) const -> bool = default;
};

auto canonicalTarget(std::string name) -> std::string
{
    if (name.starts_with("fresh_"))
        name.erase(0, 6);
    return name;
}

auto catalogNumber(const soci::row& row, std::size_t index) -> std::string
{
    switch (row.get_properties(index).get_data_type())
    {
    case soci::dt_integer:
        return std::to_string(row.get<int>(index));
    case soci::dt_long_long:
        return std::to_string(row.get<long long>(index));
    case soci::dt_unsigned_long_long:
        return std::to_string(row.get<unsigned long long>(index));
    case soci::dt_string:
        return row.get<std::string>(index);
    default:
        throw std::logic_error{"Unexpected numeric catalog type"};
    }
}

auto actualShape(soci::session& session, Backend backend, const std::string& name) -> ActualTableShape
{
    ActualTableShape shape;
    if (backend == Backend::Sqlite)
    {
        std::map<int, std::string> primary;
        soci::rowset<soci::row> columns =
            (session.prepare << "PRAGMA table_info(" + migration_detail::quoteIdentifier(backend, name) + ")");
        for (const auto& column : columns)
        {
            const auto columnName = column.get<std::string>(1);
            shape.columns.push_back(columnName + "|" + column.get<std::string>(2) + "|" + catalogNumber(column, 3));
            const auto ordinal = std::stoi(catalogNumber(column, 5));
            if (ordinal > 0)
                primary.emplace(ordinal, columnName);
        }
        for (const auto& [ordinal, column] : primary)
        {
            static_cast<void>(ordinal);
            shape.primaryKey.push_back(column);
        }
        soci::rowset<soci::row> keys =
            (session.prepare << "PRAGMA foreign_key_list(" + migration_detail::quoteIdentifier(backend, name) + ")");
        for (const auto& key : keys)
        {
            shape.foreignKeys.push_back(key.get<std::string>(3) + "|" + canonicalTarget(key.get<std::string>(2)) + "|" +
                                        key.get<std::string>(4) + "|" + key.get<std::string>(5) + "|" +
                                        key.get<std::string>(6) + "|" +
                                        std::to_string(std::stoi(catalogNumber(key, 1)) + 1));
        }
    }
    else
    {
        const bool postgres = backend == Backend::Postgres;
        const std::string schema = postgres ? "current_schema()" : "DATABASE()";
        soci::rowset<soci::row> columns =
            (session.prepare << "SELECT column_name," + std::string{postgres ? "data_type" : "column_type"} +
                                    ",is_nullable,character_maximum_length," +
                                    std::string{postgres ? "is_identity" : "extra"} +
                                    " FROM information_schema.columns WHERE table_schema=" + schema +
                                    " AND table_name=:name",
             soci::use(name, "name"));
        for (const auto& column : columns)
        {
            const auto length = column.get_indicator(3) == soci::i_null ? std::string{} : catalogNumber(column, 3);
            shape.columns.push_back(column.get<std::string>(0) + "|" + column.get<std::string>(1) + "|" +
                                    column.get<std::string>(2) + "|" + length + "|" + column.get<std::string>(4));
        }
        soci::rowset<soci::row> primary =
            (session.prepare
                 << "SELECT k.column_name FROM information_schema.key_column_usage k "
                    "JOIN information_schema.table_constraints c ON c.constraint_schema=k.constraint_schema "
                    "AND c.constraint_name=k.constraint_name AND c.table_name=k.table_name "
                    "WHERE k.table_schema=" +
                        schema +
                        " AND k.table_name=:name "
                        "AND c.constraint_type='PRIMARY KEY' ORDER BY k.ordinal_position",
             soci::use(name, "name"));
        for (const auto& column : primary)
            shape.primaryKey.push_back(column.get<std::string>(0));
        const auto query =
            postgres ?
                "SELECT k.column_name,c.table_name,c.column_name,r.update_rule,r.delete_rule,k.ordinal_position "
                "FROM information_schema.key_column_usage k "
                "JOIN information_schema.referential_constraints r ON r.constraint_schema=k.constraint_schema "
                "AND r.constraint_name=k.constraint_name "
                "JOIN information_schema.key_column_usage c ON c.constraint_schema=r.unique_constraint_schema "
                "AND c.constraint_name=r.unique_constraint_name AND c.ordinal_position=k.position_in_unique_constraint "
                "WHERE k.table_schema=current_schema() AND k.table_name=:name" :
                "SELECT "
                "k.column_name,k.referenced_table_name,k.referenced_column_name,r.update_rule,r.delete_rule,k.ordinal_"
                "position "
                "FROM information_schema.key_column_usage k "
                "JOIN information_schema.referential_constraints r ON r.constraint_schema=k.constraint_schema "
                "AND r.constraint_name=k.constraint_name AND r.table_name=k.table_name "
                "WHERE k.table_schema=DATABASE() AND k.table_name=:name AND k.referenced_table_name IS NOT NULL";
        soci::rowset<soci::row> keys = (session.prepare << query, soci::use(name, "name"));
        for (const auto& key : keys)
        {
            shape.foreignKeys.push_back(key.get<std::string>(0) + "|" + canonicalTarget(key.get<std::string>(1)) + "|" +
                                        key.get<std::string>(2) + "|" + key.get<std::string>(3) + "|" +
                                        key.get<std::string>(4) + "|" + catalogNumber(key, 5));
        }
    }
    std::ranges::sort(shape.columns);
    std::ranges::sort(shape.foreignKeys);
    return shape;
}
} // namespace

class NativeGenerationTest : public DatabaseTest<generated_models::Before>
{
public:
    auto session() -> soci::session&
    {
        return migration_detail::DatabaseAccess::session(connection);
    }
    auto quote(std::string_view name) const -> std::string
    {
        return migration_detail::quoteIdentifier(GetParam().type, name);
    }
    auto snapshot(orm::model::SchemaView view) const -> SchemaSnapshot
    {
        return capture(view, std::array{GetParam().type});
    }
    auto initial(const SchemaSnapshot& target) -> Migration
    {
        return generate(1, "initial", compare(emptySnapshot(std::array{GetParam().type}), target)).migration;
    }
    auto nextIdentityValue() -> int
    {
        int value{};
        session() << "SELECT MAX(" + quote("id") + ") FROM " + quote("generation_identity"), soci::into(value);
        return value;
    }
    auto assertPreflightRejects(std::string_view object)
    {
        const auto before = snapshot(generated_models::Plain::view);
        const auto first = initial(before);
        Runner{connection, Catalog{first}}.up();
        session() << "INSERT INTO " + quote("generation_identity") + " VALUES(1,'kept')";
        const auto table = quote("generation_identity");
        if (object == "index")
            session() << "CREATE INDEX " + quote("unmanaged_index") + " ON " + table + " (" + quote("id") + ")";
        else if (object == "column")
            session() << "ALTER TABLE " + table + " ADD COLUMN extra INTEGER";
        else if (object == "view")
            session() << "CREATE VIEW " + quote("unmanaged_view") + " AS SELECT " + quote("id") + " FROM " + table;
        else if (GetParam().type == Backend::Sqlite)
        {
            session() << "CREATE TRIGGER unmanaged_trigger BEFORE UPDATE ON " + table + " BEGIN SELECT 1; END";
        }
        else if (GetParam().type == Backend::Postgres)
        {
            session() << "CREATE FUNCTION unmanaged_trigger_fn() RETURNS TRIGGER LANGUAGE plpgsql AS $$BEGIN RETURN "
                         "NEW; END;$$";
            session() << "CREATE TRIGGER unmanaged_trigger BEFORE UPDATE ON " + table +
                             " FOR EACH ROW EXECUTE FUNCTION unmanaged_trigger_fn()";
        }
        else
            session() << "CREATE TRIGGER unmanaged_trigger BEFORE UPDATE ON " + table +
                             " FOR EACH ROW SET NEW.label=NEW.label";
        const auto after = snapshot(generated_models::Renamed::view);
        GenerationDecisions decisions;
        decisions.columnRenames.push_back({"generation_identity", "label", "caption"});
        const auto second = generate(2, "rename", compare(before, after, decisions), decisions).migration;
        Runner runner{connection, Catalog{first, second}};
        try
        {
            runner.up();
            FAIL() << "Unmanaged " << object << " must block execution";
        }
        catch (const MigrationError& error)
        {
            EXPECT_EQ(error.getCode(), ErrorCode::InvalidSchema) << error.what();
        }
        EXPECT_EQ(runner.status().currentVersion, 1);
        EXPECT_FALSE(runner.status().incomplete);
        std::string label;
        session() << "SELECT " + quote("label") + " FROM " + table, soci::into(label);
        EXPECT_EQ(label, "kept");
        EXPECT_FALSE(migration_detail::DatabaseAccess::active(connection));
    }
};

TEST_P(NativeGenerationTest, RenamesBackfillsWideningAndOrderedKeysMatchFreshModelTables)
{
    const auto before = snapshot(generated_models::Before::view);
    const auto after = snapshot(generated_models::After::view);
    const auto first = initial(before);
    Runner{connection, Catalog{first}}.up();
    session() << "INSERT INTO " + quote("generation_groups") + " (" + quote("first") + "," + quote("second") +
                     ") VALUES(5,12)";
    session() << "INSERT INTO " + quote("generation_records") + " (" + quote("id") + "," + quote("group_first") + "," +
                     quote("group_second") + "," + quote("label") + "," + quote("score") +
                     ") VALUES(1,5,12,NULL,30000)";
    GenerationDecisions decisions;
    decisions.tableRenames = {{"generation_groups", "generation_teams"}, {"generation_records", "generation_accounts"}};
    decisions.columnRenames = {{"generation_accounts", "label", "title"}};
    decisions.backfills = {{.table = "generation_accounts",
                            .column = "title",
                            .constant = GenerationLiteral{std::string{"unknown"}},
                            .sourceColumn = {},
                            .expressions = {}},
                           {.table = "generation_accounts",
                            .column = "enabled",
                            .constant = GenerationLiteral{true},
                            .sourceColumn = {},
                            .expressions = {}}};
    decisions = approveChanges(before, after, std::move(decisions));
    const auto second = generate(2, "models", compare(before, after, decisions), decisions).migration;
    Runner runner{connection, Catalog{first, second}};
    ASSERT_NO_THROW(runner.up());
    EXPECT_EQ(runner.status().currentVersion, 2);
    auto context = connection.orm<generated_models::After>();
    orm::Query<generated_models::NewAccount> query;
    const auto accounts = context.select(query);
    ASSERT_EQ(accounts.size(), 1);
    EXPECT_EQ(accounts[0].id, 1);
    EXPECT_EQ(accounts[0].title, "unknown");
    EXPECT_EQ(accounts[0].score, 30000);
    EXPECT_TRUE(accounts[0].enabled);
    ASSERT_TRUE(accounts[0].group);
    EXPECT_EQ(accounts[0].group->first, 5);
    EXPECT_EQ(accounts[0].group->second, 12);
    context.insert(generated_models::NewAccount{2, generated_models::NewGroup{5, 12}, "larger", 40000, true});
    auto fresh = connection.orm<generated_models::Fresh>();
    fresh.createTable<generated_models::FreshGroup>();
    fresh.createTable<generated_models::FreshAccount>();
    EXPECT_EQ(actualShape(session(), GetParam().type, "generation_teams"),
              actualShape(session(), GetParam().type, "fresh_generation_teams"));
    EXPECT_EQ(actualShape(session(), GetParam().type, "generation_accounts"),
              actualShape(session(), GetParam().type, "fresh_generation_accounts"));
    const auto parent = actualShape(session(), GetParam().type, "generation_teams");
    EXPECT_EQ(parent.primaryKey, (std::vector<std::string>{"second", "first"}));
    EXPECT_THROW(session() << "INSERT INTO " + quote("generation_accounts") + " (" + quote("id") + "," +
                                  quote("group_first") + "," + quote("group_second") + "," + quote("title") + "," +
                                  quote("score") + "," + quote("enabled") + ") VALUES(3,99,12,'bad',1,TRUE)",
                 soci::soci_error);
    // A subsequent migration must accept the generated schema as its source,
    // including backend-created FK indexes and renamed constraint ownership.
    auto later = after;
    auto& accountTable =
        *std::ranges::find(later.backends.at(GetParam().type).tables, "generation_accounts", &PhysicalTable::name);
    accountTable.columns.push_back({.name = "notes",
                                    .logicalType = orm::model::ColumnType::String,
                                    .sqlType = "TEXT",
                                    .nullable = true,
                                    .maxStringLength = {},
                                    .autoIncrement = false});
    const auto third = generate(3, "notes", compare(after, later)).migration;
    Runner subsequent{connection, Catalog{first, second, third}};
    ASSERT_NO_THROW(subsequent.up());
    int withEmptyNotes{};
    session() << "SELECT COUNT(*) FROM " + quote("generation_accounts") + " WHERE " + quote("notes") + " IS NULL",
        soci::into(withEmptyNotes);
    EXPECT_EQ(withEmptyNotes, 2);
}

TEST_P(NativeGenerationTest, IdentityTogglePreservesRowsAndAdvancesPastExistingKeys)
{
    const auto plain = snapshot(generated_models::Plain::view);
    const auto automatic = snapshot(generated_models::Identity::view);
    const auto first = initial(plain);
    Runner{connection, Catalog{first}}.up();
    const auto table = quote("generation_identity");
    session() << "INSERT INTO " + table + " VALUES(25,'first')";
    auto enable = approveChanges(plain, automatic);
    const auto second = generate(2, "enable_identity", compare(plain, automatic, enable), enable).migration;
    Runner{connection, Catalog{first, second}}.up();
    session() << "INSERT INTO " + table + " (" + quote("label") + ") VALUES('second')";
    EXPECT_EQ(nextIdentityValue(), 26);
    auto disable = approveChanges(automatic, plain);
    const auto third = generate(3, "disable_identity", compare(automatic, plain, disable), disable).migration;
    Runner{connection, Catalog{first, second, third}}.up();
    session() << "INSERT INTO " + table + " VALUES(100,'third')";
    if (GetParam().type != Backend::Sqlite)
    {
        EXPECT_THROW(session() << "INSERT INTO " + table + " (" + quote("label") + ") VALUES('missing-id')",
                     soci::soci_error);
    }
    const auto fourth = generate(4, "enable_again", compare(plain, automatic, enable), enable).migration;
    Runner runner{connection, Catalog{first, second, third, fourth}};
    ASSERT_NO_THROW(runner.up());
    session() << "INSERT INTO " + table + " (" + quote("label") + ") VALUES('fourth')";
    EXPECT_EQ(nextIdentityValue(), 101);
    int count{};
    session() << "SELECT COUNT(*) FROM " + table, soci::into(count);
    EXPECT_EQ(count, 4);
    EXPECT_EQ(runner.status().currentVersion, 4);
}

TEST_P(NativeGenerationTest, UnmanagedIndexBlocksSourceMutation)
{
    assertPreflightRejects("index");
}
TEST_P(NativeGenerationTest, ExtraColumnBlocksSourceMutation)
{
    assertPreflightRejects("column");
}
TEST_P(NativeGenerationTest, UnmanagedTriggerBlocksSourceMutation)
{
    assertPreflightRejects("trigger");
}
TEST_P(NativeGenerationTest, UnmanagedViewBlocksSourceMutation)
{
    assertPreflightRejects("view");
}

TEST_P(NativeGenerationTest, CrossTableBackfillReadsSourceBeforeItsTableIsDropped)
{
    const auto before = snapshot(generated_models::CrossExpressionBefore::view);
    const auto after = snapshot(generated_models::CrossExpressionAfter::view);
    const auto first = initial(before);
    Runner{connection, Catalog{first}}.up();
    session() << "INSERT INTO " + quote("expression_a_source") + " VALUES(1,17),(2,42)";
    session() << "INSERT INTO " + quote("expression_z_target") + " VALUES(9),(10)";
    GenerationDecisions decisions;
    ColumnTransformation fill;
    fill.table = "expression_z_target";
    fill.column = "copied";
    fill.expressions.emplace(GetParam().type,
                             "(SELECT MAX(" + quote("value") + ") FROM " + quote("expression_a_source") + ")");
    decisions.backfills.push_back(std::move(fill));
    decisions = approveChanges(before, after, std::move(decisions));
    const auto second = generate(2, "cross_table", compare(before, after, decisions), decisions).migration;
    Runner runner{connection, Catalog{first, second}};
    ASSERT_NO_THROW(runner.up());
    auto context = connection.orm<generated_models::CrossExpressionAfter>();
    orm::Query<generated_models::FilledExpressionTarget> query;
    const auto rows = context.select(query);
    ASSERT_EQ(rows.size(), 2);
    EXPECT_EQ(rows[0].copied, 42);
    EXPECT_EQ(rows[1].copied, 42);
    EXPECT_FALSE(migration_detail::DatabaseAccess::runtime(connection).tableExists(session(), "expression_a_source"));
    EXPECT_EQ(runner.status().currentVersion, 2);
}

TEST_P(NativeGenerationTest, QualifiedExpressionsUseSourceNamesBeforeBothRenameDirections)
{
    const auto before = snapshot(generated_models::QualifiedBefore::view);
    const auto after = snapshot(generated_models::QualifiedAfter::view);
    const auto first = initial(before);
    Runner{connection, Catalog{first}}.up();
    session() << "INSERT INTO " + quote("expression_before") + " VALUES(1,13),(2,0)";
    GenerationDecisions decisions;
    decisions.tableRenames = {{"expression_before", "expression_after"}};
    decisions.columnRenames = {{"expression_after", "value", "result"}};
    ColumnTransformation forward;
    forward.table = "expression_after";
    forward.column = "result";
    forward.expressions.emplace(GetParam().type, quote("expression_before") + "." + quote("value") + " + 5");
    decisions.conversions.push_back(std::move(forward));
    ColumnTransformation reverse;
    reverse.table = "expression_before";
    reverse.column = "value";
    reverse.expressions.emplace(GetParam().type, quote("expression_after") + "." + quote("result") + " - 5");
    decisions.downConversions.push_back(std::move(reverse));
    const auto second = generate(2, "qualified_names", compare(before, after, decisions), decisions).migration;
    ASSERT_TRUE(second.scripts.at(GetParam().type).down);
    Runner runner{connection, Catalog{first, second}};
    ASSERT_NO_THROW(runner.up());
    int firstValue{}, secondValue{};
    session() << "SELECT " + quote("result") + " FROM " + quote("expression_after") + " WHERE " + quote("id") + "=1",
        soci::into(firstValue);
    session() << "SELECT " + quote("result") + " FROM " + quote("expression_after") + " WHERE " + quote("id") + "=2",
        soci::into(secondValue);
    EXPECT_EQ(firstValue, 18);
    EXPECT_EQ(secondValue, 5);
    ASSERT_NO_THROW(runner.down(1));
    session() << "SELECT " + quote("value") + " FROM " + quote("expression_before") + " WHERE " + quote("id") + "=1",
        soci::into(firstValue);
    session() << "SELECT " + quote("value") + " FROM " + quote("expression_before") + " WHERE " + quote("id") + "=2",
        soci::into(secondValue);
    EXPECT_EQ(firstValue, 13);
    EXPECT_EQ(secondValue, 0);
    EXPECT_EQ(runner.status().currentVersion, 1);
}

TEST_P(NativeGenerationTest, RenamedColumnAndNewColumnUsingItsSourceNamePreserveDistinctData)
{
    const auto before = snapshot(generated_models::ReusedBefore::view);
    const auto after = snapshot(generated_models::ReusedAfter::view);
    const auto first = initial(before);
    Runner{connection, Catalog{first}}.up();
    session() << "INSERT INTO " + quote("generation_column_reuse") + " (" + quote("id") + "," + quote("a") +
                     ") VALUES(1,13),(2,17)";
    GenerationDecisions decisions;
    decisions.columnRenames = {{"generation_column_reuse", "a", "b"}};
    decisions.backfills = {{.table = "generation_column_reuse", .column = "a", .constant = std::int64_t{91}}};
    const auto second = generate(2, "reused_column", compare(before, after, decisions), decisions).migration;
    EXPECT_FALSE(second.scripts.at(GetParam().type).down);
    Runner runner{connection, Catalog{first, second}};
    ASSERT_NO_THROW(runner.up());
    auto context = connection.orm<generated_models::ReusedAfter>();
    orm::Query<generated_models::ReusedColumnAfter> query;
    auto rows = context.select(query);
    std::ranges::sort(rows, {}, &generated_models::ReusedColumnAfter::id);
    ASSERT_EQ(rows.size(), 2U);
    EXPECT_EQ(rows[0].id, 1);
    EXPECT_EQ(rows[0].b, 13);
    EXPECT_EQ(rows[0].a, 91);
    EXPECT_EQ(rows[1].id, 2);
    EXPECT_EQ(rows[1].b, 17);
    EXPECT_EQ(rows[1].a, 91);
    EXPECT_EQ(runner.status().currentVersion, 2);
    EXPECT_THROW(runner.down(1), MigrationError);
    EXPECT_EQ(runner.status().currentVersion, 2);
}

INSTANTIATE_TEST_SUITE_P(DatabaseTest, NativeGenerationTest, orm::test::fixtures::conformanceBackendTestConfigs,
                         orm::test::fixtures::backendTestName);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(NativeGenerationTest);
