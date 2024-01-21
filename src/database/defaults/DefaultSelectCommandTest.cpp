#include "DefaultSelectCommand.hpp"

#include <gtest/gtest.h>

#include "orm-cxx/query.hpp"
#include "tests/ModelsDefinitions.hpp"
#include "tests/utils/FakeDatabase.hpp"

namespace
{
const std::string selectSql = "SELECT * FROM models_ModelWithFloat;";

const std::string selectSqlWithLimit = "SELECT * FROM models_ModelWithFloat LIMIT 10;";

const std::string selectSqlWithOffset = "SELECT * FROM models_ModelWithFloat OFFSET 10;";

const std::string selectSqlWithLimitAndOffset = "SELECT * FROM models_ModelWithFloat OFFSET 10 LIMIT 10;";
}

class DefaultSelectCommandTest : public ::testing::Test
{
public:
    orm::db::commands::DefaultSelectCommand command;

    orm::Query<models::ModelWithFloat> query;
};

TEST_F(DefaultSelectCommandTest, select)
{
    orm::Query<models::ModelWithFloat> query;

    EXPECT_EQ(command.select(orm::Database::getQueryData(query)), selectSql);
}

TEST_F(DefaultSelectCommandTest, selectWithLimit)
{
    orm::Query<models::ModelWithFloat> query;

    query.limit(10);

    EXPECT_EQ(command.select(orm::Database::getQueryData(query)), selectSqlWithLimit);
}

TEST_F(DefaultSelectCommandTest, selectWithOffset)
{
    orm::Query<models::ModelWithFloat> query;

    query.offset(10);

    EXPECT_EQ(command.select(orm::Database::getQueryData(query)), selectSqlWithOffset);
}

TEST_F(DefaultSelectCommandTest, selectWithLimitAndOffset)
{
    orm::Query<models::ModelWithFloat> query;

    query.limit(10).offset(10);

    EXPECT_EQ(command.select(orm::Database::getQueryData(query)), selectSqlWithLimitAndOffset);

    orm::Query<models::ModelWithFloat> query2;

    query2.offset(10).limit(10);

    EXPECT_EQ(command.select(orm::Database::getQueryData(query2)), selectSqlWithLimitAndOffset);
}
