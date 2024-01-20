#include "DatabaseTest.hpp"

class SimpleModelTest : public DatabaseTest
{
};

TEST_P(SimpleModelTest, shouldExecuteQueryWhenTableIsEmpty_returnEmptyVector)
{
    createTable<models::SomeDataModel>();
    orm::Query<models::SomeDataModel> query;

    database.insert(std::vector<models::SomeDataModel>{});

    EXPECT_EQ(database.select(query).size(), 0);
}

TEST_P(SimpleModelTest, shouldExecuteInsertQuery)
{
    createTable<models::SomeDataModel>();
    orm::Query<models::SomeDataModel> query;

    database.insert(generateSomeDataModels<models::SomeDataModel>(modelCount));

    EXPECT_EQ(database.select(query).size(), modelCount);
}

TEST_P(SimpleModelTest, shouldExecuteInsertQueryWithOptional)
{
    createTable<models::ModelWithOptional>();

    database.insert(generateSomeDataModels<models::ModelWithOptional>(modelCount));

    orm::Query<models::ModelWithOptional> queryForOptional;
    EXPECT_EQ(database.select(queryForOptional).size(), modelCount);
}

TEST_P(SimpleModelTest, shouldExecuteInsertQueryAndSelectQuery_valuesShouldBeSame)
{
    createTable<models::SomeDataModel>();
    auto models = generateSomeDataModels<models::SomeDataModel>(modelCount);
    orm::Query<models::SomeDataModel> query;

    database.insert(models);

    auto returnedModels = database.select(query);

    for (std::size_t i = 0; i < modelCount; i++)
    {
        EXPECT_EQ(models[i].field1, returnedModels[i].field1);
        EXPECT_EQ(models[i].field2, returnedModels[i].field2);
    }
}

TEST_P(SimpleModelTest, shouldExecuteInsertQueryAndSelectQueryWithOptional_valuesShouldBeSame)
{
    createTable<models::ModelWithOptional>();
    auto models = generateSomeDataModels<models::ModelWithOptional>(modelCount);

    database.insert(models);

    orm::Query<models::ModelWithOptional> queryForOptional;
    auto returnedModels = database.select(queryForOptional);

    for (std::size_t i = 0; i < modelCount; i++)
    {
        EXPECT_EQ(models[i].field1, returnedModels[i].field1);
        EXPECT_EQ(models[i].field2, returnedModels[i].field2);
    }
}

TEST_P(SimpleModelTest, shouldExecuteInsertQueryAndSelectQueryWithFloat_valuesShouldBeSame)
{
    createTable<models::ModelWithFloat>();
    auto models = generateSomeDataModels<models::ModelWithFloat>(modelCount);

    database.insert(models);

    orm::Query<models::ModelWithFloat> queryForOptional;
    auto returnedModels = database.select(queryForOptional);

    for (std::size_t i = 0; i < modelCount; i++)
    {
        EXPECT_EQ(models[i].field1, returnedModels[i].field1);
        EXPECT_EQ(models[i].field2, returnedModels[i].field2);
    }
}

TEST_P(SimpleModelTest, shouldExecuteInsertQueryAndSelectQueryWithFloatAndOptional_valuesShouldBeSame)
{
    createTable<models::ModelWithOptionalFloat>();
    auto models = generateSomeDataModels<models::ModelWithOptionalFloat>(modelCount);
    models[0].field3 = std::nullopt;

    database.insert(models);

    orm::Query<models::ModelWithOptionalFloat> queryForOptional;
    auto returnedModels = database.select(queryForOptional);

    for (std::size_t i = 0; i < modelCount; i++)
    {
        EXPECT_EQ(models[i].field1, returnedModels[i].field1);
        EXPECT_EQ(models[i].field2, returnedModels[i].field2);
    }
}

TEST_P(SimpleModelTest, shouldThrowWhileReadingNullValueToNotNullableField)
{
    createTable<models::ModelWithOptionalFloat>();
    auto models = std::vector<models::ModelWithOptionalFloat>{{std::nullopt, "test", 1.0f}, {2, "test2", 2.0f}};
    database.insert(models);
    orm::Query<models::ModelWithFloat> queryForOptionalFloat;
    EXPECT_THROW(database.select(queryForOptionalFloat), std::runtime_error);
}

INSTANTIATE_TEST_SUITE_P(DatabaseTest, SimpleModelTest, connectionStrings);
