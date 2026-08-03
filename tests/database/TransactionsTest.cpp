#include "DatabaseTest.hpp"

class TransactionsTest : public DatabaseTest<models::Schema>
{
};

TEST_P(TransactionsTest, insertInCommitedTransaction_shouldInsertObjects)
{
    createTable<models::SomeDataModel>();
    auto models = generateSomeDataModels<models::SomeDataModel>(modelCount);
    orm::Query<models::SomeDataModel> query;

    for (std::size_t i = 0; i < models.size(); ++i)
    {
        models[i].field1 = static_cast<int>(i);
    }
    query.orderBy(orm::query::asc(orm::query::col("field1")));

    database.beginTransaction();

    database.insert(models);

    database.commitTransaction();

    auto returnedModels = database.select(query);

    EXPECT_EQ(returnedModels.size(), modelCount);

    for (std::size_t i = 0; i < modelCount; i++)
    {
        EXPECT_EQ(models[i].field1, returnedModels[i].field1);
        EXPECT_EQ(models[i].field2, returnedModels[i].field2);
    }
}

TEST_P(TransactionsTest, insertInRolledBackTransaction_shouldNotInsertObjects)
{
    createTable<models::SomeDataModel>();
    auto models = generateSomeDataModels<models::SomeDataModel>(modelCount);
    orm::Query<models::SomeDataModel> query;

    database.beginTransaction();

    database.insert(models);

    database.rollbackTransaction();

    auto returnedModels = database.select(query);

    EXPECT_EQ(returnedModels.size(), 0);
}

TEST_P(TransactionsTest, beginTransactionWithActiveTransaction_shouldThrowTransactionError)
{
    database.beginTransaction();

    try
    {
        database.beginTransaction();
        FAIL() << "Expected DatabaseError";
    }
    catch (const orm::DatabaseError& error)
    {
        EXPECT_EQ(error.getCode(), orm::DatabaseErrorCode::Transaction);
        EXPECT_EQ(error.getBackendType(), GetParam().type);
        EXPECT_EQ(error.getOperation(), "begin transaction");
    }
}

TEST_P(TransactionsTest, commitWithoutActiveTransaction_shouldThrowTransactionError)
{
    try
    {
        database.commitTransaction();
        FAIL() << "Expected DatabaseError";
    }
    catch (const orm::DatabaseError& error)
    {
        EXPECT_EQ(error.getCode(), orm::DatabaseErrorCode::Transaction);
        EXPECT_EQ(error.getBackendType(), GetParam().type);
        EXPECT_EQ(error.getOperation(), "commit transaction");
    }
}

TEST_P(TransactionsTest, rollbackWithoutActiveTransaction_shouldThrowTransactionError)
{
    try
    {
        database.rollbackTransaction();
        FAIL() << "Expected DatabaseError";
    }
    catch (const orm::DatabaseError& error)
    {
        EXPECT_EQ(error.getCode(), orm::DatabaseErrorCode::Transaction);
        EXPECT_EQ(error.getBackendType(), GetParam().type);
        EXPECT_EQ(error.getOperation(), "rollback transaction");
    }
}

TEST_P(TransactionsTest, disconnectWithActiveTransaction_shouldClearTransactionState)
{
    database.beginTransaction();

    ASSERT_NO_THROW(database.disconnect());
    EXPECT_FALSE(database.isConnected());

    ASSERT_NO_THROW(database.connect(GetParam().type, GetParam().connectionString));
    ASSERT_NO_THROW(database.beginTransaction());
    EXPECT_NO_THROW(database.rollbackTransaction());
}

INSTANTIATE_TEST_SUITE_P(DatabaseTest, TransactionsTest, backendTestConfigs, backendTestName);
