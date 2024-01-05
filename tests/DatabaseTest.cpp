#include "orm-cxx/database.hpp"

#include <format>
#include <gtest/gtest.h>

#include "orm-cxx/query.hpp"
#include "soci/sqlite3/soci-sqlite3.h"

namespace
{
const std::string connectionString = "sqlite3://:memory:";
}

struct SomeDataModel
{
    int field1;
    std::string field2;
};

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
    database.insertObjects(std::vector<SomeDataModel>{{1, "test"}});

    EXPECT_EQ(database.executeQuery(query).size(), 1);
}
