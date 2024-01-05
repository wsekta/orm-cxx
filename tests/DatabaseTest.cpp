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
};

struct ModelWithOptional
{
    std::optional<int> field1;
    std::optional<std::string> field2;
};

auto generateSomeDataModels(int count) -> std::vector<SomeDataModel>
{
    std::vector<SomeDataModel> result;

    for (int i = 0; i < count; i++)
    {
        result.push_back({faker::Number::integer(512), faker::Lorem::word()});
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

TEST_F(DatabaseTest, shouldExecuteQuery)
{
    database.connect(connectionString);
    database.createTable<SomeDataModel>();
    database.insertObjects(generateSomeDataModels(modelCount));

    EXPECT_EQ(database.executeQuery(query).size(), modelCount);

    database.deleteTable<SomeDataModel>();
}

TEST_F(DatabaseTest, shouldExecuteInsertWithOptional)
{
    database.connect(connectionString);
    database.createTable<ModelWithOptional>();

    database.insertObjects(std::vector<ModelWithOptional>{{1, "test"}, {std::nullopt, std::nullopt}});

    orm::Query<ModelWithOptional> queryForOptional;
    EXPECT_EQ(database.executeQuery(queryForOptional).size(), 2);

    database.deleteTable<ModelWithOptional>();
}
