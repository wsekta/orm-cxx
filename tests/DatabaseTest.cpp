#include "orm-cxx/database.hpp"

#include <gtest/gtest.h>

#include "faker-cxx/Lorem.h"
#include "faker-cxx/Number.h"
#include "orm-cxx/query.hpp"

namespace
{
const std::string connectionString = "sqlite3://:memory:";
int modelCount = 10;
}

struct SomeDataModel
{
    int field1;
    std::string field2;
    double field3;
};

struct ModelWithOptional
{
    std::optional<int> field1;
    std::optional<std::string> field2;
    std::optional<double> field3;
};

struct ModelWithId
{
    int id;
    int field1;
    std::string field2;
};

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
    orm::Query<SomeDataModel> query;
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

    database.insertObjects(std::vector<SomeDataModel>{});
}

TEST_F(DatabaseTest, shouldCreateTable)
{
    database.connect(connectionString);

    database.createTable<SomeDataModel>();
}

TEST_F(DatabaseTest, shouldDeleteTable)
{
    database.connect(connectionString);

    database.createTable<SomeDataModel>();

    database.deleteTable<SomeDataModel>();
}

TEST_F(DatabaseTest, shouldExecuteQueryWhenTableIsEmpty_returnEmptyVector)
{
    database.connect(connectionString);
    database.createTable<SomeDataModel>();
    database.insertObjects(std::vector<SomeDataModel>{});

    EXPECT_EQ(database.executeQuery(query).size(), 0);
}

TEST_F(DatabaseTest, shouldExecuteInsertQuery)
{
    database.connect(connectionString);
    database.createTable<SomeDataModel>();
    database.insertObjects(generateSomeDataModels<SomeDataModel>(modelCount));

    EXPECT_EQ(database.executeQuery(query).size(), modelCount);

    database.deleteTable<SomeDataModel>();
}

TEST_F(DatabaseTest, shouldExecuteInsertQueryWithOptional)
{
    database.connect(connectionString);
    database.createTable<ModelWithOptional>();

    database.insertObjects(generateSomeDataModels<ModelWithOptional>(modelCount));

    orm::Query<ModelWithOptional> queryForOptional;
    EXPECT_EQ(database.executeQuery(queryForOptional).size(), modelCount);

    database.deleteTable<ModelWithOptional>();
}

TEST_F(DatabaseTest, shouldExecuteInsertQueryAndSelectQuery_valuesShouldBeSame)
{
    database.connect(connectionString);
    database.createTable<SomeDataModel>();
    auto models = generateSomeDataModels<SomeDataModel>(modelCount);
    database.insertObjects(models);
    auto returnedModels = database.executeQuery(query);

    for (std::size_t i = 0; i < modelCount; i++)
    {
        EXPECT_EQ(models[i].field1, returnedModels[i].field1);
        EXPECT_EQ(models[i].field2, returnedModels[i].field2);
    }

    database.deleteTable<SomeDataModel>();
}

TEST_F(DatabaseTest, shouldExecuteInsertQueryAndSelectQueryWithOptional_valuesShouldBeSame)
{
    database.connect(connectionString);
    database.createTable<ModelWithOptional>();
    auto models = generateSomeDataModels<ModelWithOptional>(modelCount);
    models[0].field1 = std::nullopt;
    models[0].field2 = std::nullopt;
    database.insertObjects(models);
    orm::Query<ModelWithOptional> queryForOptional;
    auto returnedModels = database.executeQuery(queryForOptional);

    for (std::size_t i = 0; i < models.size(); i++)
    {
        EXPECT_EQ(models[i].field1, returnedModels[i].field1);
        EXPECT_EQ(models[i].field2, returnedModels[i].field2);
    }

    database.deleteTable<ModelWithOptional>();
}

TEST_F(DatabaseTest, shouldCreateTableWithIdColumn)
{
    database.connect(connectionString);
    database.createTable<ModelWithId>();

    database.deleteTable<ModelWithId>();
}
