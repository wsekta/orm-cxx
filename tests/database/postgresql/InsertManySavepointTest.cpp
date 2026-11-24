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

using InsertManySavepointSchema = orm::Schema<models::ModelWithId>;

namespace
{
auto makeRows(int count, int firstId = 1) -> std::vector<models::ModelWithId>
{
    std::vector<models::ModelWithId> rows;
    rows.reserve(static_cast<std::size_t>(count));

    for (int index = 0; index < count; ++index)
    {
        rows.push_back(models::ModelWithId{firstId + index, index, std::format("row-{}", index)});
    }

    return rows;
}

auto selectAll(orm::OrmContext<InsertManySavepointSchema>& database) -> std::vector<models::ModelWithId>
{
    orm::Query<models::ModelWithId> query;
    query.orderBy(orm::query::asc(orm::query::col<&models::ModelWithId::id>()));
    return database.select(query);
}
} // namespace

class InsertManySavepointTest : public DatabaseTest<InsertManySavepointSchema>
{
};

TEST_P(InsertManySavepointTest, callerTransactionKeepsItsEarlierRowAndCommitsTheBatch)
{
    createTable<models::ModelWithId>();
    connection.beginTransaction();
    database.insert(models::ModelWithId{1, 0, "caller"});

    const auto batch = makeRows(70, 2);
    ASSERT_NO_THROW(database.insertMany(std::span<const models::ModelWithId>{batch}));

    const auto insideTransaction = selectAll(database);
    ASSERT_EQ(insideTransaction.size(), 71);
    EXPECT_EQ(insideTransaction.front().id, 1);
    EXPECT_EQ(insideTransaction.front().field2, "caller");
    EXPECT_EQ(insideTransaction.back().id, 71);

    connection.commitTransaction();

    const auto afterCommit = selectAll(database);
    ASSERT_EQ(afterCommit.size(), 71);
    EXPECT_EQ(afterCommit.front().field2, "caller");
    EXPECT_EQ(afterCommit.back().id, 71);
}

TEST_P(InsertManySavepointTest, callerRollbackAfterSuccessfulInsertManyRemovesTheBatch)
{
    createTable<models::ModelWithId>();
    connection.beginTransaction();
    database.insert(models::ModelWithId{1, 0, "caller"});

    const auto batch = makeRows(40, 2);
    ASSERT_NO_THROW(database.insertMany(std::span<const models::ModelWithId>{batch}));
    ASSERT_EQ(selectAll(database).size(), 41);

    connection.rollbackTransaction();

    EXPECT_TRUE(selectAll(database).empty());
}

TEST_P(InsertManySavepointTest, duplicateInSecondChunkRecoversTheCallerTransaction)
{
    createTable<models::ModelWithId>();
    connection.beginTransaction();
    database.insert(models::ModelWithId{1, 0, "baseline"});

    auto rows = makeRows(70, 2);
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

    const auto afterFailure = selectAll(database);
    ASSERT_EQ(afterFailure.size(), 1);
    EXPECT_EQ(afterFailure[0].id, 1);
    EXPECT_EQ(afterFailure[0].field2, "baseline");

    EXPECT_NO_THROW(database.insert(models::ModelWithId{1001, 5, "after-recovery"}));
    EXPECT_NO_THROW(connection.commitTransaction());

    const auto afterCommit = selectAll(database);
    ASSERT_EQ(afterCommit.size(), 2);
    EXPECT_EQ(afterCommit[0].id, 1);
    EXPECT_EQ(afterCommit[1].id, 1001);
}

TEST_P(InsertManySavepointTest, secondInsertManyAfterRecoveredFailureSucceeds)
{
    createTable<models::ModelWithId>();
    connection.beginTransaction();

    auto failing = makeRows(70, 2);
    failing[40].id = failing[0].id;

    EXPECT_THROW(database.insertMany(std::span<const models::ModelWithId>{failing}), orm::DatabaseError);

    const auto recovered = makeRows(3, 100);
    ASSERT_NO_THROW(database.insertMany(std::span<const models::ModelWithId>{recovered}));
    ASSERT_NO_THROW(connection.commitTransaction());

    const auto stored = selectAll(database);
    ASSERT_EQ(stored.size(), 3);
    EXPECT_EQ(stored[0].id, 100);
    EXPECT_EQ(stored[2].id, 102);
}

TEST_P(InsertManySavepointTest, consecutiveBatchesInsideOneCallerTransactionAllCommit)
{
    createTable<models::ModelWithId>();
    connection.beginTransaction();

    const auto first = makeRows(35, 1);
    const auto second = makeRows(35, 36);
    ASSERT_NO_THROW(database.insertMany(std::span<const models::ModelWithId>{first}));
    ASSERT_NO_THROW(database.insertMany(std::span<const models::ModelWithId>{second}));

    ASSERT_NO_THROW(connection.commitTransaction());

    EXPECT_EQ(selectAll(database).size(), 70);
}

TEST_P(InsertManySavepointTest, preFailedCallerTransactionIsRejectedWithoutBeingReset)
{
    createTable<models::ModelWithId>();
    connection.beginTransaction();
    database.insert(models::ModelWithId{1, 0, "first"});

    EXPECT_THROW(database.insert(models::ModelWithId{1, 0, "duplicate"}), orm::DatabaseError);

    const auto rows = makeRows(3, 2);

    try
    {
        database.insertMany(std::span<const models::ModelWithId>{rows});
        FAIL() << "Expected orm::DatabaseError";
    }
    catch (const orm::DatabaseError& error)
    {
        EXPECT_EQ(error.getCode(), orm::DatabaseErrorCode::Transaction);
        EXPECT_EQ(error.getOperation(), "insert many");
    }

    // The rejected call must not clear the failed flag, so commit still refuses.
    EXPECT_THROW(connection.commitTransaction(), orm::DatabaseError);

    EXPECT_NO_THROW(connection.rollbackTransaction());
    EXPECT_TRUE(selectAll(database).empty());
}

INSTANTIATE_TEST_SUITE_P(Postgresql, InsertManySavepointTest, ::testing::Values(postgresqlBackendTestConfig),
                         backendTestName);
