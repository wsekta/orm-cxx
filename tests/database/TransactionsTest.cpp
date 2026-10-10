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

class TransactionsTest : public DatabaseTest<models::Schema>
{
};

TEST_P(TransactionsTest, insertInCommitedTransaction_shouldInsertObjects)
{
    createTable<models::SomeDataModel>();
    auto models = orm::generateSomeDataModels<models::SomeDataModel>(modelCount);
    orm::Query<models::SomeDataModel> query;

    for (std::size_t i = 0; i < models.size(); ++i)
    {
        models[i].field1 = static_cast<int>(i);
    }
    query.orderBy(orm::query::asc(orm::query::col<&models::SomeDataModel::field1>()));

    connection.beginTransaction();

    database.insert(models);

    connection.commitTransaction();

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
    auto models = orm::generateSomeDataModels<models::SomeDataModel>(modelCount);
    orm::Query<models::SomeDataModel> query;

    connection.beginTransaction();

    database.insert(models);

    connection.rollbackTransaction();

    auto returnedModels = database.select(query);

    EXPECT_EQ(returnedModels.size(), 0);
}

TEST_P(TransactionsTest, beginTransactionWithActiveTransaction_shouldThrowTransactionError)
{
    connection.beginTransaction();

    try
    {
        connection.beginTransaction();
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
        connection.commitTransaction();
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
        connection.rollbackTransaction();
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
    connection.beginTransaction();

    ASSERT_NO_THROW(connection.disconnect());
    EXPECT_FALSE(connection.isConnected());

    ASSERT_NO_THROW(connection.connect(GetParam().type, GetParam().connectionString));
    ASSERT_NO_THROW(connection.beginTransaction());
    EXPECT_NO_THROW(connection.rollbackTransaction());
}

INSTANTIATE_TEST_SUITE_P(DatabaseTest, TransactionsTest, backendTestConfigs, backendTestName);
