#include "orm-cxx/database.hpp"

#include <gtest/gtest.h>

#include "faker-cxx/Lorem.h"
#include "faker-cxx/Number.h"
#include "ModelsDefinitions.hpp"
#include "orm-cxx/query.hpp"

namespace
{
const std::string connectionString = "sqlite3://:memory:";
int modelCount = 10;
}

template <typename T>
auto generateSomeDataModels(int count) -> std::vector<T>
{
    std::vector<T> result;

    for (std::size_t i = 0; i < count; i++)
    {
        result.emplace_back(faker::Number::integer(512), faker::Lorem::word(), faker::Number::decimal(-1.0, 1.0));
    }

    return result;
}

class DatabaseTest : public ::testing::Test
{
public:
    orm::Query<models::SomeDataModel> query;
    orm::Database database;
};

TEST_F(DatabaseTest, shouldConnectToDatabase)
{
    database.connect(connectionString);

    EXPECT_EQ(database.getBackendType(), orm::db::BackendType::Sqlite);
}

TEST_F(DatabaseTest, shouldInsertObjects)
{
    database.connect(connectionString);

    database.insert(std::vector<models::SomeDataModel>{});
}

TEST_F(DatabaseTest, shouldCreateTable)
{
    database.connect(connectionString);

    database.createTable<models::SomeDataModel>();
}

TEST_F(DatabaseTest, shouldDeleteTable)
{
    database.connect(connectionString);

    database.createTable<models::SomeDataModel>();

    database.deleteTable<models::SomeDataModel>();
}

TEST_F(DatabaseTest, shouldExecuteQueryWhenTableIsEmpty_returnEmptyVector)
{
    database.connect(connectionString);
    database.createTable<models::SomeDataModel>();
    database.insert(std::vector<models::SomeDataModel>{});

    EXPECT_EQ(database.query(query).size(), 0);
}

TEST_F(DatabaseTest, shouldExecuteInsertQuery)
{
    database.connect(connectionString);
    database.createTable<models::SomeDataModel>();
    database.insert(generateSomeDataModels<models::SomeDataModel>(modelCount));

    EXPECT_EQ(database.query(query).size(), modelCount);

    database.deleteTable<models::SomeDataModel>();
}

TEST_F(DatabaseTest, shouldExecuteInsertQueryWithOptional)
{
    database.connect(connectionString);
    database.createTable<models::ModelWithOptional>();

    database.insert(generateSomeDataModels<models::ModelWithOptional>(modelCount));

    orm::Query<models::ModelWithOptional> queryForOptional;
    EXPECT_EQ(database.query(queryForOptional).size(), modelCount);

    database.deleteTable<models::ModelWithOptional>();
}

TEST_F(DatabaseTest, shouldExecuteInsertQueryAndSelectQuery_valuesShouldBeSame)
{
    database.connect(connectionString);
    database.createTable<models::SomeDataModel>();
    auto models = generateSomeDataModels<models::SomeDataModel>(modelCount);
    database.insert(models);
    auto returnedModels = database.query(query);

    for (std::size_t i = 0; i < modelCount; i++)
    {
        EXPECT_EQ(models[i].field1, returnedModels[i].field1);
        EXPECT_EQ(models[i].field2, returnedModels[i].field2);
    }

    database.deleteTable<models::SomeDataModel>();
}

TEST_F(DatabaseTest, shouldExecuteInsertQueryAndSelectQueryWithOptional_valuesShouldBeSame)
{
    database.connect(connectionString);
    database.createTable<models::ModelWithOptional>();
    auto models = generateSomeDataModels<models::ModelWithOptional>(modelCount);
    models[0].field1 = std::nullopt;
    models[0].field2 = std::nullopt;
    database.insert(models);
    orm::Query<models::ModelWithOptional> queryForOptional;
    auto returnedModels = database.query(queryForOptional);

    for (std::size_t i = 0; i < models.size(); i++)
    {
        EXPECT_EQ(models[i].field1, returnedModels[i].field1);
        EXPECT_EQ(models[i].field2, returnedModels[i].field2);
    }

    database.deleteTable<models::ModelWithOptional>();
}

TEST_F(DatabaseTest, shouldExecuteInsertQueryAndSelectQueryWithFloat_valuesShouldBeSame)
{
    database.connect(connectionString);
    database.createTable<models::ModelWithFloat>();
    auto models = std::vector<models::ModelWithFloat>{{1, "test", 1.0f}, {2, "test2", 2.0f}};
    database.insert(models);
    orm::Query<models::ModelWithFloat> queryForFloat;
    auto returnedModels = database.query(queryForFloat);

    for (std::size_t i = 0; i < models.size(); i++)
    {
        EXPECT_EQ(models[i].field1, returnedModels[i].field1);
        EXPECT_EQ(models[i].field2, returnedModels[i].field2);
        EXPECT_EQ(models[i].field3, returnedModels[i].field3);
    }

    database.deleteTable<models::ModelWithFloat>();
}

TEST_F(DatabaseTest, shouldExecuteInsertQueryAndSelectQueryWithOptionalFloat_valuesShouldBeSame)
{
    database.connect(connectionString);
    database.createTable<models::ModelWithOptionalFloat>();
    auto models = std::vector<models::ModelWithOptionalFloat>{{1, "test", std::nullopt}, {2, "test2", 2.0f}};
    database.insert(models);
    orm::Query<models::ModelWithOptionalFloat> queryForOptionalFloat;
    auto returnedModels = database.query(queryForOptionalFloat);

    for (std::size_t i = 0; i < models.size(); i++)
    {
        EXPECT_EQ(models[i].field1, returnedModels[i].field1);
        EXPECT_EQ(models[i].field2, returnedModels[i].field2);
        EXPECT_EQ(models[i].field3, returnedModels[i].field3);
    }

    database.deleteTable<models::ModelWithOptionalFloat>();
}

TEST_F(DatabaseTest, shouldCreateTableWithIdColumn)
{
    database.connect(connectionString);
    database.createTable<models::ModelWithId>();

    database.deleteTable<models::ModelWithId>();
}

TEST_F(DatabaseTest, shouldCreateTableWithOverwrittenIdColumn)
{
    database.connect(connectionString);
    database.createTable<models::ModelWithOverwrittenId>();

    database.deleteTable<models::ModelWithOverwrittenId>();
}

TEST_F(DatabaseTest, shouldExecuteInsertQueryAndSelectQueryWithNamesMapping_valuesShouldBeSame)
{
    database.connect(connectionString);
    database.createTable<models::ModelWithIdAndNamesMapping>();
    auto models = std::vector<models::ModelWithIdAndNamesMapping>{{1, 1, "test"}, {2, 2, "test2"}};
    database.insert(models);
    orm::Query<models::ModelWithIdAndNamesMapping> queryForNamesMapping;
    auto returnedModels = database.query(queryForNamesMapping);

    for (std::size_t i = 0; i < models.size(); i++)
    {
        EXPECT_EQ(models[i].field1, returnedModels[i].field1);
        EXPECT_EQ(models[i].field2, returnedModels[i].field2);
    }

    database.deleteTable<models::ModelWithIdAndNamesMapping>();
}

TEST_F(DatabaseTest, shouldCreateTableWithRelatedModel)
{
    database.connect(connectionString);
    database.createTable<models::ModelWithId>();
    database.createTable<models::ModelRelatedToOtherModel>();

    database.deleteTable<models::ModelWithId>();
    database.deleteTable<models::ModelRelatedToOtherModel>();
}

TEST_F(DatabaseTest, shouldThrowWhileReadingNullValueToNotNullableField)
{
    database.connect(connectionString);
    database.createTable<models::ModelWithOptionalFloat>();
    auto models = std::vector<models::ModelWithOptionalFloat>{{std::nullopt, "test", 1.0f}, {2, "test2", 2.0f}};
    database.insert(models);
    orm::Query<models::ModelWithFloat> queryForOptionalFloat;
    EXPECT_THROW(database.query(queryForOptionalFloat), std::runtime_error);

    database.deleteTable<models::ModelWithOptionalFloat>();
}