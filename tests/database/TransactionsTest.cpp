#include "DatabaseTest.hpp"

class TransactionsTest : public DatabaseTest
{
};

TEST_P(TransactionsTest, insertInCommitedTransaction_shouldInsertObjects)
{
    createTable<models::SomeDataModel>();
    auto models = generateSomeDataModels<models::SomeDataModel>(modelCount);
    orm::Query<models::SomeDataModel> query;

    database.beginTransaction();

    database.insert(models);

    database.commitTransaction();

    auto returnedModels = database.select(query);

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

INSTANTIATE_TEST_SUITE_P(DatabaseTest, TransactionsTest, connectionStrings);
