module;

#include "tests/UnitTestPrelude.hpp"

module orm;

import :internal;
import :test_support;
import :foundation;
import :model;
import :expressions;
import :dynamic_query;
import :static_plan;
import :sql;
import :database;

using namespace orm::test::fixtures;

namespace insert_many_guard_models
{
struct TextRecord
{
    int id;
    std::string text;
    std::int64_t big;

    inline static constexpr orm::reflection::FixedString table_name{"insert_many_guard_text"};
};

/** Only an auto-increment primary key, so the batch generator has no bound column to render. */
struct AutoOnlyRecord
{
    int id;

    inline static constexpr orm::reflection::FixedString table_name{"insert_many_guard_auto_only"};
    inline static constexpr auto auto_increment_columns = orm::autoIncrement<&AutoOnlyRecord::id>();
};
} // namespace insert_many_guard_models

using InsertManyGuardSchema =
    orm::Schema<models::ModelWithId, models::ModelWithOverwrittenId, models::ModelRelatedToCompositeIdModel,
                insert_many_guard_models::TextRecord, insert_many_guard_models::AutoOnlyRecord>;

namespace
{
template <typename T>
auto selectAllOrderedById(orm::OrmContext<InsertManyGuardSchema>& database) -> std::vector<T>
{
    orm::Query<T> query;
    query.orderBy(orm::query::asc(orm::query::col<&T::id>()));
    return database.select(query);
}

template <typename T>
auto countRows(orm::OrmContext<InsertManyGuardSchema>& database) -> std::size_t
{
    orm::Query<T> query;
    return database.select(query).size();
}

auto makeModelWithIdRows(int count, int firstId = 1) -> std::vector<models::ModelWithId>
{
    std::vector<models::ModelWithId> rows;
    rows.reserve(static_cast<std::size_t>(count));

    for (int index = 0; index < count; ++index)
    {
        rows.push_back(models::ModelWithId{firstId + index, index, std::format("row-{}", index)});
    }

    return rows;
}

/**
 * A custom generator that implements only the abstract single-statement operations. It keeps the
 * inherited batch default, so a batch insert has to surface that rejection instead of rendering SQL.
 */
class SingleStatementGenerator final : public orm::db::CommandGenerator
{
public:
    auto createTable(orm::model::ModelView /*model*/) const -> std::string override
    {
        return "CREATE TABLE single_statement()";
    }
    auto dropTable(orm::model::ModelView /*model*/) const -> std::string override
    {
        return "DROP TABLE single_statement";
    }
    auto insert(orm::model::ModelView /*model*/) const -> std::string override
    {
        return "INSERT INTO single_statement DEFAULT VALUES";
    }
    auto select(orm::model::ModelView /*model*/,
                const orm::db::SelectSpec& /*spec*/) const -> orm::db::SelectStatement override
    {
        return {.sql = "SELECT 1", .parameters = {}};
    }
    auto update(orm::model::ModelView /*model*/,
                const orm::db::UpdateSpec& /*spec*/) const -> orm::db::Statement override
    {
        return {.sql = "UPDATE single_statement", .parameters = {}};
    }
    auto remove(orm::model::ModelView /*model*/,
                const orm::db::Predicate& /*predicate*/) const -> orm::db::Statement override
    {
        return {.sql = "DELETE FROM single_statement", .parameters = {}};
    }
};

/**
 * PostgreSQL generator that records every batch the executor asks it to render and optionally pads
 * the statement with whitespace ahead of the terminator. Without padding it keeps the production
 * SQL, so the recorded batch sizes expose the planner's chunk width; with padding it makes the
 * rendered statement wider than the planner's estimate, which exposes the executor's shrink step.
 */
class RecordingGenerator final : public orm::db::DefaultCommandGenerator
{
public:
    RecordingGenerator(const orm::db::SqlDialect& dialect, std::size_t paddingPerRow)
        : DefaultCommandGenerator{dialect, orm::db::BackendType::Postgres}, paddingPerRow{paddingPerRow}
    {
    }

    auto insertMany(orm::model::ModelView model, const orm::db::InsertManySpec& spec) const -> std::string override
    {
        auto sql = DefaultCommandGenerator::insertMany(model, spec);
        renderedBatchSizes.push_back(spec.rowCount);

        if (paddingPerRow != 0)
        {
            sql.insert(sql.size() - 1, paddingPerRow * spec.rowCount, ' ');
        }

        return sql;
    }

    /** Row count of every rendered batch, in call order. */
    [[nodiscard]] auto batches() const -> const std::vector<std::size_t>&
    {
        return renderedBatchSizes;
    }

    [[nodiscard]] auto largestBatch() const -> std::size_t
    {
        return renderedBatchSizes.empty() ? 0 : *std::ranges::max_element(renderedBatchSizes);
    }

private:
    std::size_t paddingPerRow;
    mutable std::vector<std::size_t> renderedBatchSizes;
};

/**
 * Delegating PostgreSQL dialect with deliberately oversized bind markers.
 *
 * The planner sizes a chunk from the dialect's marker width, so a wider marker lowers how many rows
 * fit the generated SQL budget without a model of dozens of long-named columns.
 */
class PaddingMarkerDialect final : public orm::db::SqlDialect
{
public:
    PaddingMarkerDialect(const orm::db::SqlDialect& delegate, std::size_t padding)
        : delegate{delegate}, padding{padding}
    {
    }

    auto quoteIdentifier(std::string_view identifier) const -> std::string override
    {
        return delegate.quoteIdentifier(identifier);
    }
    auto bindMarker(std::string_view logicalName) const -> std::string override
    {
        return delegate.bindMarker(logicalName) + std::string(padding, 'x');
    }
    auto toSqlType(orm::model::ColumnType type,
                   std::optional<std::size_t> maxStringLength) const -> std::string override
    {
        return delegate.toSqlType(type, maxStringLength);
    }
    auto renderCreateTablePrefix(std::string_view tableName, bool ifNotExists) const -> std::string override
    {
        return delegate.renderCreateTablePrefix(tableName, ifNotExists);
    }
    auto renderDropTable(std::string_view tableName, bool ifExists) const -> std::string override
    {
        return delegate.renderDropTable(tableName, ifExists);
    }
    auto renderAutoIncrementPrimaryKey(std::string_view columnName) const -> std::string override
    {
        return delegate.renderAutoIncrementPrimaryKey(columnName);
    }
    auto renderPagination(const orm::db::PaginationSpec& pagination) const -> std::string override
    {
        return delegate.renderPagination(pagination);
    }
    auto renderInsertIfAbsent(const orm::db::InsertIfAbsentSpec& insert) const -> std::string override
    {
        return delegate.renderInsertIfAbsent(insert);
    }

private:
    const orm::db::SqlDialect& delegate;
    std::size_t padding;
};

/** Isolated-schema PostgreSQL runtime that advertises a smaller bind budget than the backend has. */
class CappedBindRuntime final : public postgresql_test::SchemaRuntime
{
public:
    CappedBindRuntime(const orm::db::BackendRuntime& delegate, std::string schema, std::size_t maxBindParameters)
        : SchemaRuntime{delegate, std::move(schema)}, bindParameterCap{maxBindParameters}
    {
    }

    auto limits(soci::session& session) const -> orm::db::BackendRuntimeLimits override
    {
        auto runtimeLimits = SchemaRuntime::limits(session);
        runtimeLimits.maxBindParameters = bindParameterCap;
        return runtimeLimits;
    }

private:
    std::size_t bindParameterCap;
};

/** A PostgreSQL provider that swaps in the supplied dialect, generator and runtime test doubles. */
class GuardProvider final : public orm::db::BackendProvider
{
public:
    GuardProvider(const orm::db::BackendProvider& postgres, const orm::db::BackendRuntime& runtime,
                  const orm::db::SqlDialect& dialect, const orm::db::CommandGenerator& generator)
        : postgres{postgres}, runtimeValue{runtime}, dialectValue{dialect}, generatorValue{generator}
    {
    }

    auto type() const noexcept -> orm::db::BackendType override
    {
        return postgres.type();
    }
    auto capabilities() const noexcept -> const orm::db::BackendCapabilities& override
    {
        return postgres.capabilities();
    }
    auto dialect() const noexcept -> const orm::db::SqlDialect& override
    {
        return dialectValue;
    }
    auto runtime() const noexcept -> const orm::db::BackendRuntime& override
    {
        return runtimeValue;
    }
    auto commandGenerator() const noexcept -> const orm::db::CommandGenerator& override
    {
        return generatorValue;
    }
    auto compiledSqlFlavor() const noexcept -> orm::db::CompiledSqlFlavor override
    {
        return postgres.compiledSqlFlavor();
    }

private:
    const orm::db::BackendProvider& postgres;
    const orm::db::BackendRuntime& runtimeValue;
    const orm::db::SqlDialect& dialectValue;
    const orm::db::CommandGenerator& generatorValue;
};

/**
 * Runs the body against a context whose backend is the isolated test schema driven by the supplied
 * dialect, generator and optional bind-parameter cap. The registry is assembled through the public
 * backend-extension surface, exactly like an application-supplied provider.
 */
template <typename Body>
auto withGuardedContext(postgresql_test::PostgresqlTestSchema& schema, const orm::db::SqlDialect& dialect,
                        const orm::db::CommandGenerator& generator, std::optional<std::size_t> bindParameterCap,
                        Body&& body) -> void
{
    const orm::db::CommandGeneratorFactory builtins;
    const auto& postgres = builtins.getBackend(orm::db::BackendType::Postgres);

    std::unique_ptr<orm::db::BackendRuntime> runtime;
    if (bindParameterCap.has_value())
    {
        runtime = std::make_unique<CappedBindRuntime>(postgres.runtime(), schema.schemaName(), *bindParameterCap);
    }
    else
    {
        runtime = std::make_unique<postgresql_test::SchemaRuntime>(postgres.runtime(), schema.schemaName());
    }

    std::vector<std::unique_ptr<orm::db::BackendProvider>> providers;
    providers.push_back(std::make_unique<GuardProvider>(postgres, *runtime, dialect, generator));

    const auto config = schema.connectionConfig();
    orm::Database guarded{config, orm::db::CommandGeneratorFactory{std::move(providers)}};
    auto context = guarded.orm<InsertManyGuardSchema>();
    body(context);
}
} // namespace

/**
 * Guard tests for the batch planner and error translation that are distinct from the parity cases:
 * composite relation-key parameter names, the encoded-value chunk split, the bind-parameter and
 * generated-SQL budgets, the batch rejection of a generator that never opted into batch support,
 * and the exact error shape of the guards this lane added.
 *
 * PostgreSQL only: the file intentionally has no SQLite instantiation.
 */
class InsertManyGuardTest : public DatabaseTest<InsertManyGuardSchema>
{
};

TEST_P(InsertManyGuardTest, compositeToOneParameterNamesAreBoundAcrossChunks)
{
    createTable<models::ModelWithOverwrittenId>();
    createTable<models::ModelRelatedToCompositeIdModel>();

    // ModelWithOverwrittenId declares (field1, field2) as a composite primary key, so each row binds
    // "field3_field1" and "field3_field2" instead of a single relation key.
    const std::vector<models::ModelWithOverwrittenId> parents{
        {10, 1, "key-one"},
        {20, 2, "key-two"},
    };
    database.insert(parents);

    std::vector<models::ModelRelatedToCompositeIdModel> rows;
    rows.reserve(35);
    for (int index = 0; index < 35; ++index)
    {
        rows.push_back(models::ModelRelatedToCompositeIdModel{index + 1, std::format("related-{}", index),
                                                              parents[static_cast<std::size_t>(index % 2)]});
    }

    ASSERT_NO_THROW(database.insertMany(rows));

    const auto stored = selectAllOrderedById<models::ModelRelatedToCompositeIdModel>(database);
    ASSERT_EQ(stored.size(), rows.size());

    for (const auto& row : stored)
    {
        const auto expected = parents[static_cast<std::size_t>(row.id - 1) % 2];
        EXPECT_EQ(row.field3.field1, expected.field1);
        EXPECT_EQ(row.field3.field2, expected.field2);
    }

    EXPECT_EQ(countRows<models::ModelWithOverwrittenId>(database), parents.size());
}

TEST_P(InsertManyGuardTest, oversizedEncodedStringIsRejectedWithoutRows)
{
    createTable<insert_many_guard_models::TextRecord>();
    const std::string oversized(4 * 1024 * 1024 + 1, 'x');
    const std::vector<insert_many_guard_models::TextRecord> rows{{1, oversized, 7}};

    try
    {
        database.insertMany(rows);
        FAIL() << "Expected orm::DatabaseError";
    }
    catch (const orm::DatabaseError& error)
    {
        EXPECT_EQ(error.getCode(), orm::DatabaseErrorCode::UnsupportedFeature);
        EXPECT_EQ(error.getBackendType(), orm::db::BackendType::Postgres);
        EXPECT_EQ(error.getOperation(), "insert many");
        // This guard is decided before the driver sees the row, so it has no native SQLSTATE.
        EXPECT_FALSE(error.getNativeCode().has_value());
    }

    EXPECT_EQ(countRows<insert_many_guard_models::TextRecord>(database), 0);
}

TEST_P(InsertManyGuardTest, encodedValueBudgetSplitsTheBatchAndKeepsEveryRow)
{
    createTable<insert_many_guard_models::TextRecord>();

    // Each row fits the 4 MiB encoded-parameter budget on its own, but three rows together do not.
    const std::string payload(1536 * 1024, 'z');
    const std::vector<insert_many_guard_models::TextRecord> rows{
        {1, payload, 1},
        {2, payload, 2},
        {3, payload, 3},
    };

    ASSERT_NO_THROW(database.insertMany(rows));

    const auto stored = selectAllOrderedById<insert_many_guard_models::TextRecord>(database);
    ASSERT_EQ(stored.size(), rows.size());

    for (std::size_t index = 0; index < rows.size(); ++index)
    {
        EXPECT_EQ(stored[index].id, rows[index].id);
        EXPECT_EQ(stored[index].text, payload);
        EXPECT_EQ(stored[index].big, rows[index].big);
    }
}

TEST_P(InsertManyGuardTest, ownTransactionConstraintFailureKeepsTheOriginalErrorShapeAndRollsBack)
{
    createTable<models::ModelWithId>();

    auto rows = makeModelWithIdRows(70);
    rows[40].id = rows[0].id;

    try
    {
        database.insertMany(rows);
        FAIL() << "Expected orm::DatabaseError";
    }
    catch (const orm::DatabaseError& error)
    {
        EXPECT_EQ(error.getCode(), orm::DatabaseErrorCode::Constraint);
        EXPECT_EQ(error.getBackendType(), orm::db::BackendType::Postgres);
        EXPECT_EQ(error.getOperation(), "insert many");
        ASSERT_TRUE(error.getNativeCode().has_value());
        EXPECT_EQ(error.getNativeCode().value(), "23505");
    }

    EXPECT_EQ(countRows<models::ModelWithId>(database), 0);

    // The rollback of the operation-owned transaction succeeded, so the connection stays usable.
    ASSERT_NO_THROW(database.insertMany(makeModelWithIdRows(2, 100)));
    EXPECT_EQ(countRows<models::ModelWithId>(database), 2);
}

TEST_P(InsertManyGuardTest, bindParameterBudgetBelowTheModelWidthRejectsTheBatch)
{
    createTable<models::ModelWithId>();

    const orm::db::CommandGeneratorFactory builtins;
    const auto& postgres = builtins.getBackend(orm::db::BackendType::Postgres);
    std::optional<orm::DatabaseError> rejection;

    // ModelWithId binds three columns, so a backend advertising two bind parameters cannot hold a row.
    withGuardedContext(*postgresqlTestSchema(), postgres.dialect(), postgres.commandGenerator(), 2,
                       [&](orm::OrmContext<InsertManyGuardSchema>& guarded)
                       {
                           try
                           {
                               guarded.insertMany(makeModelWithIdRows(3));
                           }
                           catch (const orm::DatabaseError& error)
                           {
                               rejection = error;
                           }
                       });

    ASSERT_TRUE(rejection.has_value()) << "Expected orm::DatabaseError";
    EXPECT_EQ(rejection->getCode(), orm::DatabaseErrorCode::UnsupportedFeature);
    EXPECT_EQ(rejection->getBackendType(), orm::db::BackendType::Postgres);
    EXPECT_EQ(rejection->getOperation(), "insert many");
    EXPECT_STREQ(rejection->what(), "A single row exceeds the backend bind-parameter budget");
    EXPECT_FALSE(rejection->getNativeCode().has_value());

    EXPECT_EQ(countRows<models::ModelWithId>(database), 0);
}

TEST_P(InsertManyGuardTest, singleRowAboveTheGeneratedSqlBudgetIsRejected)
{
    createTable<models::ModelWithId>();

    const orm::db::CommandGeneratorFactory builtins;
    const auto& postgres = builtins.getBackend(orm::db::BackendType::Postgres);

    // Three oversized markers already exceed the 64 KiB generated-SQL budget for a single row.
    const PaddingMarkerDialect dialect{postgres.dialect(), 70 * 1024};
    const RecordingGenerator generator{postgres.dialect(), 0};
    std::optional<orm::DatabaseError> rejection;

    withGuardedContext(*postgresqlTestSchema(), dialect, generator, std::nullopt,
                       [&](orm::OrmContext<InsertManyGuardSchema>& guarded)
                       {
                           try
                           {
                               guarded.insertMany(makeModelWithIdRows(1));
                           }
                           catch (const orm::DatabaseError& error)
                           {
                               rejection = error;
                           }
                       });

    ASSERT_TRUE(rejection.has_value()) << "Expected orm::DatabaseError";
    EXPECT_EQ(rejection->getCode(), orm::DatabaseErrorCode::UnsupportedFeature);
    EXPECT_EQ(rejection->getBackendType(), orm::db::BackendType::Postgres);
    EXPECT_EQ(rejection->getOperation(), "insert many");
    EXPECT_STREQ(rejection->what(), "A single row exceeds the generated SQL budget");
    // The planner rejects the batch before rendering or opening a transaction, so nothing is prepared.
    EXPECT_TRUE(generator.batches().empty());
    EXPECT_EQ(countRows<models::ModelWithId>(database), 0);
}

TEST_P(InsertManyGuardTest, markerWidthPlanningShrinksChunksAndKeepsEveryRow)
{
    createTable<models::ModelWithId>();

    const orm::db::CommandGeneratorFactory builtins;
    const auto& postgres = builtins.getBackend(orm::db::BackendType::Postgres);

    // One row fits the SQL budget, the 32-row planner maximum does not, so the width walk lowers the
    // chunk below the batch size before any statement is rendered.
    const PaddingMarkerDialect dialect{postgres.dialect(), 6 * 1024};
    const RecordingGenerator generator{postgres.dialect(), 0};
    const auto rows = makeModelWithIdRows(12);

    withGuardedContext(*postgresqlTestSchema(), dialect, generator, std::nullopt,
                       [&](orm::OrmContext<InsertManyGuardSchema>& guarded) { guarded.insertMany(rows); });

    // A single 12-row statement would be well past the budget, so the planner must have reduced the
    // chunk: with the width walk skipped the generator would have been asked for all twelve rows.
    ASSERT_FALSE(generator.batches().empty());
    EXPECT_LT(generator.largestBatch(), rows.size());

    const auto stored = selectAllOrderedById<models::ModelWithId>(database);
    ASSERT_EQ(stored.size(), rows.size());

    for (std::size_t index = 0; index < rows.size(); ++index)
    {
        EXPECT_EQ(stored[index].id, rows[index].id);
        EXPECT_EQ(stored[index].field1, rows[index].field1);
        EXPECT_EQ(stored[index].field2, rows[index].field2);
    }
}

TEST_P(InsertManyGuardTest, generatorWiderThanThePlannerEstimateShrinksTheChunkAndKeepsEveryRow)
{
    createTable<models::ModelWithId>();

    const orm::db::CommandGeneratorFactory builtins;
    const auto& postgres = builtins.getBackend(orm::db::BackendType::Postgres);

    // The planner allows 32 rows, but the generator's padding pushes that statement past the SQL
    // budget, so the executor drops rows until the rendered chunk fits instead of rejecting a batch.
    const RecordingGenerator generator{postgres.dialect(), 3000};
    const auto rows = makeModelWithIdRows(40);

    withGuardedContext(*postgresqlTestSchema(), postgres.dialect(), generator, std::nullopt,
                       [&](orm::OrmContext<InsertManyGuardSchema>& guarded) { guarded.insertMany(rows); });

    // Two accepted chunks alone would mean two renders; the extra renders are the shrink iterations.
    EXPECT_GT(generator.batches().size(), 2U);
    EXPECT_LT(generator.largestBatch(), rows.size());

    const auto stored = selectAllOrderedById<models::ModelWithId>(database);
    ASSERT_EQ(stored.size(), rows.size());

    for (std::size_t index = 0; index < rows.size(); ++index)
    {
        EXPECT_EQ(stored[index].id, rows[index].id);
        EXPECT_EQ(stored[index].field2, rows[index].field2);
    }
}

TEST_P(InsertManyGuardTest, generatorAboveTheSqlBudgetForOneRowIsRejected)
{
    createTable<models::ModelWithId>();

    const orm::db::CommandGeneratorFactory builtins;
    const auto& postgres = builtins.getBackend(orm::db::BackendType::Postgres);

    // Padding this large cannot be shrunk away: the executor refuses to prepare a one-row statement.
    const RecordingGenerator generator{postgres.dialect(), 70 * 1024};
    std::optional<orm::DatabaseError> rejection;

    withGuardedContext(*postgresqlTestSchema(), postgres.dialect(), generator, std::nullopt,
                       [&](orm::OrmContext<InsertManyGuardSchema>& guarded)
                       {
                           try
                           {
                               guarded.insertMany(makeModelWithIdRows(2));
                           }
                           catch (const orm::DatabaseError& error)
                           {
                               rejection = error;
                           }
                       });

    ASSERT_TRUE(rejection.has_value()) << "Expected orm::DatabaseError";
    EXPECT_EQ(rejection->getCode(), orm::DatabaseErrorCode::UnsupportedFeature);
    EXPECT_EQ(rejection->getBackendType(), orm::db::BackendType::Postgres);
    EXPECT_EQ(rejection->getOperation(), "insert many");
    EXPECT_STREQ(rejection->what(), "A single row exceeds the generated SQL budget");
    // The chunk was rendered at two rows and then re-rendered at one before the executor gave up.
    EXPECT_GE(generator.batches().size(), 2U);
    // The operation-owned transaction rolled back, so the guarded batch left no rows behind.
    EXPECT_EQ(countRows<models::ModelWithId>(database), 0);

    // The rollback succeeded, so the connection stays usable for later batches.
    ASSERT_NO_THROW(database.insertMany(makeModelWithIdRows(2, 500)));
    EXPECT_EQ(countRows<models::ModelWithId>(database), 2);
}

TEST(InsertManyGeneratorGuardTest, defaultGeneratorRejectsAnEmptyBatch)
{
    const orm::db::CommandGeneratorFactory factory;
    const auto& generator = factory.getBackend(orm::db::BackendType::Postgres).commandGenerator();
    const auto model = orm::modelView<InsertManyGuardSchema, models::ModelWithId>();

    try
    {
        const auto rejected = generator.insertMany(model, orm::db::InsertManySpec{.rowCount = 0});
        FAIL() << "Expected std::invalid_argument, got: " << rejected;
    }
    catch (const std::invalid_argument& error)
    {
        EXPECT_STREQ(error.what(), "A batch insert requires at least one row");
    }
}

TEST(InsertManyGeneratorGuardTest, defaultGeneratorRejectsAModelWithoutBoundColumns)
{
    const orm::db::CommandGeneratorFactory factory;
    const auto& generator = factory.getBackend(orm::db::BackendType::Postgres).commandGenerator();
    const auto model = orm::modelView<InsertManyGuardSchema, insert_many_guard_models::AutoOnlyRecord>();

    try
    {
        const auto rejected = generator.insertMany(model, orm::db::InsertManySpec{.rowCount = 2});
        FAIL() << "Expected std::invalid_argument, got: " << rejected;
    }
    catch (const std::invalid_argument& error)
    {
        EXPECT_STREQ(error.what(), "A batch insert requires at least one bound column");
    }
}

TEST(InsertManyGeneratorGuardTest, generatorWithoutABatchOverrideRejectsTheBatch)
{
    const SingleStatementGenerator generator;
    const auto model = orm::modelView<InsertManyGuardSchema, models::ModelWithId>();

    try
    {
        const auto rejected = generator.insertMany(model, orm::db::InsertManySpec{.rowCount = 3});
        FAIL() << "Expected orm::DatabaseError, got: " << rejected;
    }
    catch (const orm::DatabaseError& error)
    {
        EXPECT_EQ(error.getCode(), orm::DatabaseErrorCode::UnsupportedFeature);
        EXPECT_EQ(error.getBackendType(), orm::db::BackendType::Empty);
        EXPECT_EQ(error.getOperation(), "insert many");
        EXPECT_STREQ(error.what(), "batch inserts are not supported by this command generator");
        EXPECT_FALSE(error.getNativeCode().has_value());
    }
}

INSTANTIATE_TEST_SUITE_P(Postgresql, InsertManyGuardTest, ::testing::Values(postgresqlBackendTestConfig),
                         backendTestName);
