#include "orm-cxx/query.hpp"

#include <gtest/gtest.h>

struct Model
{
    int field1;
    int field2;
};

namespace
{
std::string simpleQueryString = "SELECT * FROM Model;";
auto limit = 10;
std::string queryWithLimitString = "SELECT * FROM Model LIMIT 10;";
auto offset = 5;
std::string queryWithOffsetString = "SELECT * FROM Model OFFSET 5;";
std::string queryWithOffsetAndLimitString = "SELECT * FROM Model OFFSET 5 LIMIT 10;";
}

class QueryTest : public ::testing::Test
{
public:
    orm::Query<Model> query;
};

TEST_F(QueryTest, shouldCreateSimpleQuery)
{
    EXPECT_EQ(query.buildQuery(), simpleQueryString);
}

TEST_F(QueryTest, shouldCreateQueryWithLimit)
{
    EXPECT_EQ(query.limit(limit).buildQuery(), queryWithLimitString);
}

TEST_F(QueryTest, shouldCreateQueryWithOffset)
{
    EXPECT_EQ(query.offset(offset).buildQuery(), queryWithOffsetString);
}

TEST_F(QueryTest, shouldCreateQueryWithOffsetAndLimit)
{
    EXPECT_EQ(query.offset(offset).limit(limit).buildQuery(), queryWithOffsetAndLimitString);
}