#include "orm-cxx/database.hpp"

#include <format>
#include <gtest/gtest.h>

#include "orm-cxx/query.hpp"

namespace
{
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
    EXPECT_THROW(database.connect("empty://"), std::exception);
}

TEST_F(DatabaseTest, shouldExecuteQuery)
{
    auto queryString = query.buildQuery();

    EXPECT_THROW(database.executeQuery(query), std::exception);
}