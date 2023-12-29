#include "orm-cxx/database.hpp"

#include <format>
#include <gtest/gtest.h>


namespace
{
}

class DatabaseTest : public ::testing::Test
{
};

TEST_F(DatabaseTest, shouldConnectToDatabase)
{
    orm::Database database;

    EXPECT_THROW(database.connect("empty://"), std::exception);
}