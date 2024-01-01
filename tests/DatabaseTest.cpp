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
    int field2;
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
}

TEST_F(DatabaseTest, shouldExecuteQuery)
{
    database.connect(connectionString);

    auto queryString = query.buildQuery();

    EXPECT_THROW(database.executeQuery(query), std::exception);
}

TEST_F(DatabaseTest, shouldCreateTable)
{
    database.connect(connectionString);

    database.createTable<SomeDataModel>();
}