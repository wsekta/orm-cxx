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

struct ModelWithFloat
{
    int field1;
    std::string field2;
    float field3;
};

struct ModelWithId
{
    int id;
    int field1;
    std::string field2;
};

struct ModelWithOverwrittenId
{
    int id;
    int field1;
    std::string field2;

    inline static const std::vector<std::string> id_columns = {"field1", "field2"};
};

struct ModelWithIdAndNamesMapping
{
    int id;
    int field1;
    std::string field2;

    inline static const std::map<std::string, std::string> columns_names = {
        {"field1", "some_field1_name"}, {"field2", "some_field2_name"}, {"id", "some_id_name"}};
};

struct ModelRelatedToOtherModel
{
    int id;
    int field1;
    std::string field2;
    ModelWithId field3;
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

TEST_F(DatabaseTest, shouldExecuteInsertQueryAndSelectQueryWithFloat_valuesShouldBeSame)
{
    database.connect(connectionString);
    database.createTable<ModelWithFloat>();
    auto models = std::vector<ModelWithFloat>{{1, "test", 1.0f}, {2, "test2", 2.0f}};
    database.insertObjects(models);
    orm::Query<ModelWithFloat> queryForFloat;
    auto returnedModels = database.executeQuery(queryForFloat);

    for (std::size_t i = 0; i < models.size(); i++)
    {
        EXPECT_EQ(models[i].field1, returnedModels[i].field1);
        EXPECT_EQ(models[i].field2, returnedModels[i].field2);
        EXPECT_EQ(models[i].field3, returnedModels[i].field3);
    }

    database.deleteTable<ModelWithFloat>();
}

TEST_F(DatabaseTest, shouldCreateTableWithIdColumn)
{
    database.connect(connectionString);
    database.createTable<ModelWithId>();

    database.deleteTable<ModelWithId>();
}

TEST_F(DatabaseTest, shouldCreateTableWithOverwrittenIdColumn)
{
    database.connect(connectionString);
    database.createTable<ModelWithOverwrittenId>();

    database.deleteTable<ModelWithOverwrittenId>();
}

TEST_F(DatabaseTest, shouldExecuteInsertQueryAndSelectQueryWithNamesMapping_valuesShouldBeSame)
{
    database.connect(connectionString);
    database.createTable<ModelWithIdAndNamesMapping>();
    auto models = std::vector<ModelWithIdAndNamesMapping>{{1, 1, "test"}, {2, 2, "test2"}};
    database.insertObjects(models);
    orm::Query<ModelWithIdAndNamesMapping> queryForNamesMapping;
    auto returnedModels = database.executeQuery(queryForNamesMapping);

    for (std::size_t i = 0; i < models.size(); i++)
    {
        EXPECT_EQ(models[i].field1, returnedModels[i].field1);
        EXPECT_EQ(models[i].field2, returnedModels[i].field2);
    }

    database.deleteTable<ModelWithIdAndNamesMapping>();
}

TEST_F(DatabaseTest, shouldCreateTableWithRelatedModel)
{
    database.connect(connectionString);
    database.createTable<ModelWithId>();
    database.createTable<ModelRelatedToOtherModel>();

    database.deleteTable<ModelWithId>();
    database.deleteTable<ModelRelatedToOtherModel>();
}