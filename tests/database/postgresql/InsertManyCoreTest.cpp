module;

#include "tests/UnitTestPrelude.hpp"

module orm;

import :internal;
import :config;
import :test_support;
import :foundation;
import :model;
import :expressions;
import :dynamic_query;
import :static_plan;
import :sql;
import :database;

using namespace orm::test::fixtures;

namespace insert_many_models
{
struct AutoOnly
{
    int id;

    inline static constexpr orm::reflection::FixedString table_name{"insert_many_auto_only"};
    inline static constexpr auto auto_increment_columns = orm::autoIncrement<&AutoOnly::id>();
};
} // namespace insert_many_models

using InsertManyCoreSchema = orm::Schema<models::ModelWithId, models::ModelWithOptional, insert_many_models::AutoOnly>;

namespace
{
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

template <typename Operation>
auto expectInsertManyError(Operation operation, orm::DatabaseErrorCode code) -> void
{
    try
    {
        operation();
        FAIL() << "Expected orm::DatabaseError";
    }
    catch (const orm::DatabaseError& error)
    {
        EXPECT_EQ(error.getCode(), code);
        EXPECT_EQ(error.getOperation(), "insert many");
    }
}

auto sqliteParamConfigs() -> std::vector<BackendTestConfig>
{
    std::vector<BackendTestConfig> configs;
    if constexpr (orm::config::sqliteBackendEnabled)
    {
        configs.push_back(sqliteBackendTestConfig);
    }
    return configs;
}

} // namespace

class InsertManyCoreTest : public DatabaseTest<InsertManyCoreSchema>
{
};

class InsertManyCoreSqliteTest : public DatabaseTest<InsertManyCoreSchema>
{
};

TEST_P(InsertManyCoreTest, emptyInputPerformsNoBackendWork)
{
    const std::vector<models::ModelWithId> rows;
    EXPECT_NO_THROW(database.insertMany(rows));
    EXPECT_NO_THROW(database.insertMany(std::span<const models::ModelWithId>{}));
}

TEST_P(InsertManyCoreTest, postgresqlAdvertisesTheBatchCapability)
{
    EXPECT_TRUE(connection.getBackendCapabilities().mutations.insertMany);
}

TEST_P(InsertManyCoreTest, multiChunkInsertStoresEveryRowIncludingTheTail)
{
    createTable<models::ModelWithId>();
    const auto rows = makeModelWithIdRows(70);

    database.insertMany(std::span<const models::ModelWithId>{rows});

    orm::Query<models::ModelWithId> query;
    query.orderBy(orm::query::asc(orm::query::col<&models::ModelWithId::id>()));
    const auto stored = database.select(query);

    ASSERT_EQ(stored.size(), rows.size());

    for (std::size_t index = 0; index < rows.size(); ++index)
    {
        EXPECT_EQ(stored[index].id, rows[index].id);
        EXPECT_EQ(stored[index].field1, rows[index].field1);
        EXPECT_EQ(stored[index].field2, rows[index].field2);
    }
}

TEST_P(InsertManyCoreTest, explicitSpanOfArrayIsAccepted)
{
    createTable<models::ModelWithId>();
    const std::array<models::ModelWithId, 2> rows{models::ModelWithId{1, 10, "first"},
                                                  models::ModelWithId{2, 20, "second"}};

    database.insertMany(std::span<const models::ModelWithId>{rows});

    orm::Query<models::ModelWithId> query;
    EXPECT_EQ(database.select(query).size(), 2);
}

#if defined(ORM_CXX_PERF_SEAM_AVAILABLE)
TEST_P(InsertManyCoreTest, forcedChunkSizeOneRunsTheProductionPathOverManyRows)
{
    createTable<models::ModelWithId>();
    const auto rows = makeModelWithIdRows(40);

    orm::detail::perf_seam::insertManyWithChunkRowsForTest(database, std::span<const models::ModelWithId>{rows}, 1);

    orm::Query<models::ModelWithId> query;
    query.orderBy(orm::query::asc(orm::query::col<&models::ModelWithId::id>()));
    const auto stored = database.select(query);

    ASSERT_EQ(stored.size(), rows.size());

    for (std::size_t index = 0; index < rows.size(); ++index)
    {
        EXPECT_EQ(stored[index].id, rows[index].id);
        EXPECT_EQ(stored[index].field2, rows[index].field2);
    }
}
#endif

TEST_P(InsertManyCoreTest, duplicateInSecondChunkRollsBackTheWholeBatch)
{
    createTable<models::ModelWithId>();
    database.insert(models::ModelWithId{1, 0, "baseline"});

    auto rows = makeModelWithIdRows(70, 2);
    rows[40].id = 1;

    try
    {
        database.insertMany(std::span<const models::ModelWithId>{rows});
        FAIL() << "Expected orm::DatabaseError";
    }
    catch (const orm::DatabaseError& error)
    {
        EXPECT_EQ(error.getCode(), orm::DatabaseErrorCode::Constraint);
        EXPECT_EQ(error.getOperation(), "insert many");
        ASSERT_TRUE(error.getNativeCode().has_value());
        EXPECT_EQ(error.getNativeCode().value(), "23505");
    }

    orm::Query<models::ModelWithId> query;
    const auto stored = database.select(query);
    ASSERT_EQ(stored.size(), 1);
    EXPECT_EQ(stored[0].id, 1);
    EXPECT_EQ(stored[0].field2, "baseline");
}

TEST_P(InsertManyCoreTest, modelWithoutBoundColumnsIsRejectedBeforeWriting)
{
    const std::array<insert_many_models::AutoOnly, 1> rows{insert_many_models::AutoOnly{1}};

    expectInsertManyError([this, &rows]() { database.insertMany(std::span<const insert_many_models::AutoOnly>{rows}); },
                          orm::DatabaseErrorCode::UnsupportedFeature);
}

TEST_P(InsertManyCoreTest, activeCallerTransactionIsSupportedWithoutTakingOwnership)
{
    createTable<models::ModelWithId>();
    connection.beginTransaction();

    EXPECT_NO_THROW(database.insertMany(makeModelWithIdRows(3)));

    database.insert(models::ModelWithId{4, 0, "caller"});
    EXPECT_NO_THROW(connection.commitTransaction());

    orm::Query<models::ModelWithId> query;
    EXPECT_EQ(database.select(query).size(), 4);
}

TEST_P(InsertManyCoreTest, nullAndEmptyTextRemainDistinct)
{
    createTable<models::ModelWithOptional>();
    const std::vector<models::ModelWithOptional> rows{
        models::ModelWithOptional{std::nullopt, std::nullopt, std::nullopt},
        models::ModelWithOptional{1, std::string{}, 0.0},
    };

    database.insertMany(rows);

    orm::Query<models::ModelWithOptional> nullQuery;
    nullQuery.where(orm::query::col<&models::ModelWithOptional::field2>().isNull());
    const auto nullRows = database.select(nullQuery);
    ASSERT_EQ(nullRows.size(), 1);
    EXPECT_FALSE(nullRows[0].field1.has_value());

    orm::Query<models::ModelWithOptional> emptyQuery;
    emptyQuery.where(orm::query::col<&models::ModelWithOptional::field2>() == std::string{});
    const auto emptyRows = database.select(emptyQuery);
    ASSERT_EQ(emptyRows.size(), 1);
    EXPECT_EQ(emptyRows[0].field1, std::optional<int>{1});
    EXPECT_EQ(emptyRows[0].field2, std::optional<std::string>{std::string{}});
}

TEST_P(InsertManyCoreTest, plainInsertVectorKeepsItsSequentialNonAtomicContract)
{
    createTable<models::ModelWithId>();
    database.insert(models::ModelWithId{1, 0, "baseline"});

    const std::vector<models::ModelWithId> rows{
        models::ModelWithId{2, 10, "written-before-failure"},
        models::ModelWithId{1, 20, "duplicate"},
    };

    EXPECT_THROW(database.insert(rows), orm::DatabaseError);

    orm::Query<models::ModelWithId> query;
    const auto stored = database.select(query);
    ASSERT_EQ(stored.size(), 2);
}

TEST_P(InsertManyCoreSqliteTest, emptyInputIsANoOpAndTheCapabilityStaysDisabled)
{
    EXPECT_FALSE(connection.getBackendCapabilities().mutations.insertMany);

    const std::vector<models::ModelWithId> rows;
    EXPECT_NO_THROW(database.insertMany(rows));
    EXPECT_NO_THROW(database.insertMany(std::span<const models::ModelWithId>{}));
}

TEST_P(InsertManyCoreSqliteTest, nonEmptyInputIsRejectedBeforeWriting)
{
    createTable<models::ModelWithId>();
    const auto rows = makeModelWithIdRows(3);

    expectInsertManyError([this, &rows]() { database.insertMany(std::span<const models::ModelWithId>{rows}); },
                          orm::DatabaseErrorCode::UnsupportedFeature);

    orm::Query<models::ModelWithId> query;
    EXPECT_TRUE(database.select(query).empty());
}

INSTANTIATE_TEST_SUITE_P(Postgresql, InsertManyCoreTest, ::testing::Values(postgresqlBackendTestConfig),
                         backendTestName);
// With the SQLite backend disabled this instantiation expands to zero values. GoogleTest would
// otherwise register a failing "never instantiated" verification case in PostgreSQL-only presets.
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(InsertManyCoreSqliteTest);
INSTANTIATE_TEST_SUITE_P(Sqlite, InsertManyCoreSqliteTest, ::testing::ValuesIn(sqliteParamConfigs()), backendTestName);
