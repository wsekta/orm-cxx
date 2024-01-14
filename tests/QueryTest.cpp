#include "orm-cxx/query.hpp"

#include <format>
#include <gtest/gtest.h>

#include "ModelsDefinitions.hpp"

using namespace std::string_literals;

namespace
{
auto simpleQueryString = "SELECT * FROM models_ModelWithId;"s;
auto limit = 10u;
auto queryWithLimitString = std::format("SELECT * FROM models_ModelWithId LIMIT {};", limit);
auto offset = 5u;
auto queryWithOffsetString = std::format("SELECT * FROM models_ModelWithId OFFSET {};", offset);
auto queryWithOffsetAndLimitString = std::format("SELECT * FROM models_ModelWithId OFFSET {} LIMIT {};", offset, limit);
}

class QueryTest : public ::testing::Test
{
public:
    orm::Query<models::ModelWithId> query;
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