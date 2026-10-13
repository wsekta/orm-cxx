module;

#include "tests/UnitTestPrelude.hpp"

module orm;

import :internal;
import :test_support;
import :migrations_internal;

namespace migration_value_models
{
struct IdOnly
{
    int id;
    inline static constexpr orm::reflection::FixedString table_name{"migration_target_values"};
};
struct FloatValue
{
    int id;
    float value;
    inline static constexpr orm::reflection::FixedString table_name{"migration_target_values"};
};
struct DoubleValue
{
    int id;
    double value;
    inline static constexpr orm::reflection::FixedString table_name{"migration_target_values"};
};
struct IntegerValue
{
    int id;
    int value;
    inline static constexpr orm::reflection::FixedString table_name{"migration_target_values"};
};
struct ShortValue
{
    int id;
    short value;
    inline static constexpr orm::reflection::FixedString table_name{"migration_target_values"};
};
using IdSchema = orm::Schema<IdOnly>;
using FloatSchema = orm::Schema<FloatValue>;
using DoubleSchema = orm::Schema<DoubleValue>;
using IntegerSchema = orm::Schema<IntegerValue>;
using ShortSchema = orm::Schema<ShortValue>;
} // namespace migration_value_models

namespace
{
using namespace orm::migrations;
using Backend = orm::db::BackendType;
namespace target_detail = orm::migrations::detail;
namespace value_models = migration_value_models;
} // namespace

class MigrationTargetValuesTest : public DatabaseTest<value_models::IdSchema>
{
public:
    auto session() -> soci::session&
    {
        return target_detail::DatabaseAccess::session(connection);
    }
    auto quote(std::string_view name) const -> std::string
    {
        return target_detail::quoteIdentifier(GetParam().type, name);
    }
    auto snapshot(orm::model::SchemaView models) const -> SchemaSnapshot
    {
        return capture(models, std::array{GetParam().type});
    }
    auto initial(const SchemaSnapshot& source) -> Migration
    {
        auto first = generate(1, "initial", compare(emptySnapshot(std::array{GetParam().type}), source)).migration;
        Runner{connection, Catalog{first}}.up();
        return first;
    }
    auto conversionDecisions(const SchemaSnapshot& before, const SchemaSnapshot& after,
                             std::string expression) const -> GenerationDecisions
    {
        GenerationDecisions decisions;
        ColumnTransformation conversion;
        conversion.table = "migration_target_values";
        conversion.column = "value";
        conversion.expressions.emplace(GetParam().type, std::move(expression));
        decisions.conversions.push_back(std::move(conversion));
        for (const auto& change : compare(before, after, decisions).changes)
        {
            if (change.dataLoss)
                decisions.allowDataLossUp.push_back(change.id);
        }
        return decisions;
    }
    auto rejectMigration(const Migration& first, const Migration& second) -> void
    {
        Runner runner{connection, Catalog{first, second}};
        try
        {
            runner.up();
            FAIL() << "A migration must not record values outside the target model's domain";
        }
        catch (const MigrationError& error)
        {
            EXPECT_TRUE(error.getCode() == ErrorCode::InvalidSchema || error.getCode() == ErrorCode::Execution)
                << error.what();
            EXPECT_EQ(error.getVersion(), 2);
        }
        const auto state = runner.status();
        EXPECT_EQ(state.currentVersion, 1);
        ASSERT_EQ(state.applied.size(), 1);
        const bool nontransactional =
            connection.getBackendCapabilities().migrations.atomicity == orm::db::MigrationAtomicity::NonTransactional;
        if (nontransactional)
        {
            ASSERT_TRUE(state.incomplete);
            EXPECT_EQ(state.incomplete->version, 2);
            EXPECT_EQ(state.incomplete->direction, Direction::Up);
            EXPECT_EQ(state.incomplete->backend, GetParam().type);
            EXPECT_EQ(state.incomplete->checksum, target_detail::checksum(second.scripts.at(GetParam().type)));
            EXPECT_FALSE(state.isValid());
            EXPECT_THROW(runner.up(), MigrationError);
        }
        else
        {
            EXPECT_FALSE(state.incomplete);
            EXPECT_TRUE(state.isValid());
        }
        EXPECT_TRUE(connection.isConnected());
        EXPECT_FALSE(target_detail::DatabaseAccess::active(connection));
        int count{};
        session() << "SELECT COUNT(*) FROM " + quote("migration_target_values"), soci::into(count);
        EXPECT_EQ(count, 1);
    }
};

TEST_P(MigrationTargetValuesTest, TypedFloatBackfillIsHydratableAfterDecimalQuantization)
{
    const auto before = snapshot(value_models::IdSchema::view);
    const auto after = snapshot(value_models::FloatSchema::view);
    const auto first = initial(before);
    session() << "INSERT INTO " + quote("migration_target_values") + " VALUES(1),(2)";
    GenerationDecisions decisions;
    decisions.backfills.push_back({.table = "migration_target_values",
                                   .column = "value",
                                   .constant = GenerationLiteral{0.1},
                                   .sourceColumn = {},
                                   .expressions = {}});
    const auto second = generate(2, "float_literal", compare(before, after, decisions), decisions).migration;
    Runner runner{connection, Catalog{first, second}};
    ASSERT_NO_THROW(runner.up());
    auto context = connection.orm<value_models::FloatSchema>();
    orm::Query<value_models::FloatValue> query;
    const auto values = context.select(query);
    ASSERT_EQ(values.size(), 2);
    for (const auto& value : values)
        EXPECT_FLOAT_EQ(value.value, 0.1F);
    EXPECT_EQ(runner.status().currentVersion, 2);
    EXPECT_FALSE(runner.status().incomplete);
}

TEST_P(MigrationTargetValuesTest, LossyDoubleToFloatExpressionCannotProduceAValidHistoryEntry)
{
    const auto before = snapshot(value_models::DoubleSchema::view);
    const auto after = snapshot(value_models::FloatSchema::view);
    const auto first = initial(before);
    session() << "INSERT INTO " + quote("migration_target_values") + " VALUES(1,0.1)";
    const auto decisions = conversionDecisions(before, after, quote("value"));
    const auto second = generate(2, "lossy_float", compare(before, after, decisions), decisions).migration;
    rejectMigration(first, second);
    if (GetParam().type == Backend::Sqlite || GetParam().type == Backend::Postgres)
    {
        auto context = connection.orm<value_models::DoubleSchema>();
        orm::Query<value_models::DoubleValue> query;
        const auto values = context.select(query);
        ASSERT_EQ(values.size(), 1);
        EXPECT_DOUBLE_EQ(values[0].value, 0.1);
    }
}

TEST_P(MigrationTargetValuesTest, ExactlyRepresentableDoubleToFloatExpressionRemainsHydratable)
{
    const auto before = snapshot(value_models::DoubleSchema::view);
    const auto after = snapshot(value_models::FloatSchema::view);
    const auto first = initial(before);
    session() << "INSERT INTO " + quote("migration_target_values") + " VALUES(1,0.5)";
    const auto decisions = conversionDecisions(before, after, quote("value"));
    const auto second = generate(2, "exact_float", compare(before, after, decisions), decisions).migration;
    Runner runner{connection, Catalog{first, second}};
    ASSERT_NO_THROW(runner.up());
    auto context = connection.orm<value_models::FloatSchema>();
    orm::Query<value_models::FloatValue> query;
    const auto values = context.select(query);
    ASSERT_EQ(values.size(), 1);
    EXPECT_FLOAT_EQ(values[0].value, 0.5F);
    EXPECT_EQ(runner.status().currentVersion, 2);
    EXPECT_FALSE(runner.status().incomplete);
}

TEST_P(MigrationTargetValuesTest, NonfiniteExpressionCannotProduceAValidHistoryEntry)
{
    const auto before = snapshot(value_models::IdSchema::view);
    const auto after = snapshot(value_models::DoubleSchema::view);
    const auto first = initial(before);
    session() << "INSERT INTO " + quote("migration_target_values") + " VALUES(1)";
    GenerationDecisions decisions;
    ColumnTransformation backfill;
    backfill.table = "migration_target_values";
    backfill.column = "value";
    backfill.expressions.emplace(GetParam().type,
                                 GetParam().type == Backend::Sqlite   ? "1e999" :
                                 GetParam().type == Backend::Postgres ? "CAST('Infinity' AS DOUBLE PRECISION)" :
                                                                        "EXP(1000)");
    decisions.backfills.push_back(std::move(backfill));
    const auto second = generate(2, "nonfinite", compare(before, after, decisions), decisions).migration;
    rejectMigration(first, second);
    if (GetParam().type == Backend::Sqlite || GetParam().type == Backend::Postgres)
    {
        auto context = connection.orm<value_models::IdSchema>();
        orm::Query<value_models::IdOnly> query;
        const auto values = context.select(query);
        ASSERT_EQ(values.size(), 1);
        EXPECT_EQ(values[0].id, 1);
    }
}

TEST_P(MigrationTargetValuesTest, OutOfRangeIntegerNarrowingCannotProduceAValidHistoryEntry)
{
    const auto before = snapshot(value_models::IntegerSchema::view);
    const auto after = snapshot(value_models::ShortSchema::view);
    const auto first = initial(before);
    session() << "INSERT INTO " + quote("migration_target_values") + " VALUES(1,40000)";
    const auto decisions = conversionDecisions(before, after, quote("value"));
    const auto second = generate(2, "narrow_integer", compare(before, after, decisions), decisions).migration;
    rejectMigration(first, second);
    if (GetParam().type == Backend::Sqlite || GetParam().type == Backend::Postgres)
    {
        auto context = connection.orm<value_models::IntegerSchema>();
        orm::Query<value_models::IntegerValue> query;
        const auto values = context.select(query);
        ASSERT_EQ(values.size(), 1);
        EXPECT_EQ(values[0].value, 40000);
    }
}

INSTANTIATE_TEST_SUITE_P(DatabaseTest, MigrationTargetValuesTest, orm::test::fixtures::conformanceBackendTestConfigs,
                         orm::test::fixtures::backendTestName);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(MigrationTargetValuesTest);
