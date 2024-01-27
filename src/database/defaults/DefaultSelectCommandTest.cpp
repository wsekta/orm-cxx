#include "DefaultSelectCommand.hpp"

#include <gtest/gtest.h>

#include "orm-cxx/query.hpp"
#include "tests/ModelsDefinitions.hpp"
#include "tests/utils/FakeDatabase.hpp"

namespace
{
const std::string selectSql = "SELECT models_ModelWithFloat.field1 AS models_ModelWithFloat_field1, "
                              "models_ModelWithFloat.field2 AS models_ModelWithFloat_field2, "
                              "models_ModelWithFloat.field3 AS models_ModelWithFloat_field3 "
                              "FROM models_ModelWithFloat;";

const std::string selectSqlWithLimit = "SELECT models_ModelWithFloat.field1 AS models_ModelWithFloat_field1, "
                                       "models_ModelWithFloat.field2 AS models_ModelWithFloat_field2, "
                                       "models_ModelWithFloat.field3 AS models_ModelWithFloat_field3 "
                                       "FROM models_ModelWithFloat LIMIT 10;";

const std::string selectSqlWithOffset = "SELECT models_ModelWithFloat.field1 AS models_ModelWithFloat_field1, "
                                        "models_ModelWithFloat.field2 AS models_ModelWithFloat_field2, "
                                        "models_ModelWithFloat.field3 AS models_ModelWithFloat_field3 "
                                        "FROM models_ModelWithFloat OFFSET 10;";

const std::string selectSqlWithLimitAndOffset = "SELECT models_ModelWithFloat.field1 AS models_ModelWithFloat_field1, "
                                                "models_ModelWithFloat.field2 AS models_ModelWithFloat_field2, "
                                                "models_ModelWithFloat.field3 AS models_ModelWithFloat_field3 "
                                                "FROM models_ModelWithFloat OFFSET 10 LIMIT 10;";

const std::string selectSqlWithModelRelatedToOtherModelWithoutJoining =
    "SELECT models_ModelRelatedToOtherModel.id AS models_ModelRelatedToOtherModel_id, "
    "models_ModelRelatedToOtherModel.field1 AS models_ModelRelatedToOtherModel_field1, "
    "models_ModelRelatedToOtherModel.field2 AS models_ModelRelatedToOtherModel_field2, "
    "models_ModelRelatedToOtherModel.field3_id AS models_ModelRelatedToOtherModel_field3_id"
    " FROM models_ModelRelatedToOtherModel;";

const std::string selectSqlWithModelRelatedToOtherModelWithJoining =
    "SELECT models_ModelRelatedToOtherModel.id AS models_ModelRelatedToOtherModel_id, "
    "models_ModelRelatedToOtherModel.field1 AS models_ModelRelatedToOtherModel_field1, "
    "models_ModelRelatedToOtherModel.field2 AS models_ModelRelatedToOtherModel_field2, "
    "field3.id AS field3_id, field3.field1 AS field3_field1, field3.field2 AS field3_field2"
    " FROM models_ModelRelatedToOtherModel"
    " LEFT JOIN models_ModelWithId AS field3 ON field3.id = models_ModelRelatedToOtherModel.field3_id;";
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

TEST_F(DefaultSelectCommandTest, selectWithModelRelatedToOtherModelWithoutJoining)
{
    orm::Query<models::ModelRelatedToOtherModel> query;

    query.disableJoining();

    EXPECT_EQ(command.select(orm::Database::getQueryData(query)), selectSqlWithModelRelatedToOtherModelWithoutJoining);
}

TEST_F(DefaultSelectCommandTest, selectWithModelRelatedToOtherModelWithJoining)
{
    orm::Query<models::ModelRelatedToOtherModel> query;

    EXPECT_EQ(command.select(orm::Database::getQueryData(query)), selectSqlWithModelRelatedToOtherModelWithJoining);
}
