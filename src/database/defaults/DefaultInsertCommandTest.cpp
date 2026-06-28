#include "DefaultInsertCommand.hpp"

#include <gtest/gtest.h>

#include "orm-cxx/model.hpp"
#include "tests/ModelsDefinitions.hpp"

namespace
{
const std::string insertSql =
    "INSERT INTO models_ModelWithFloat (field1, field2, field3) VALUES (:field1, :field2, :field3);";
const std::string insertSqlWithModelRelatedToOtherModel =
    "INSERT INTO models_ModelRelatedToOtherModel (id, field1, field2, field3_id) VALUES (:id, :field1, :field2, "
    ":field3_id);";
const std::string insertSqlWithModelRelatedToCompositeIdModel =
    "INSERT INTO models_ModelRelatedToCompositeIdModel (id, field1, field3_field1, field3_field2) "
    "VALUES (:id, :field1, :field3_field1, :field3_field2);";
const std::string insertSqlWithAutoIncrementId =
    "INSERT INTO models_ModelWithAutoIncrementId (field1, field2) VALUES (:field1, :field2);";
const std::string insertSqlWithMappedAutoIncrementId =
    "INSERT INTO models_ModelWithAutoIncrementIdAndNamesMapping (some_field1_name, some_field2_name) "
    "VALUES (:some_field1_name, :some_field2_name);";
const std::string insertSqlWithModelRelatedToAutoIncrementModel =
    "INSERT INTO models_ModelRelatedToAutoIncrementModel (id, field1, field3_id) VALUES (:id, :field1, :field3_id);";
} // namespace

class DefaultInsertCommandTest : public ::testing::Test
{
public:
    orm::db::commands::DefaultInsertCommand command;

    orm::Model<models::ModelWithFloat> model;
};

TEST_F(DefaultInsertCommandTest, insert)
{
    EXPECT_EQ(command.insert(model.getModelInfo()), insertSql);
}

TEST_F(DefaultInsertCommandTest, insertWithModelRelatedToOtherModel)
{
    orm::Model<models::ModelRelatedToOtherModel> model;

    EXPECT_EQ(command.insert(model.getModelInfo()), insertSqlWithModelRelatedToOtherModel);
}

TEST_F(DefaultInsertCommandTest, insertWithModelRelatedToCompositeIdModel)
{
    orm::Model<models::ModelRelatedToCompositeIdModel> model;

    EXPECT_EQ(command.insert(model.getModelInfo()), insertSqlWithModelRelatedToCompositeIdModel);
}

TEST_F(DefaultInsertCommandTest, insertWithAutoIncrementId)
{
    orm::Model<models::ModelWithAutoIncrementId> model;

    EXPECT_EQ(command.insert(model.getModelInfo()), insertSqlWithAutoIncrementId);
}

TEST_F(DefaultInsertCommandTest, insertWithMappedAutoIncrementId)
{
    orm::Model<models::ModelWithAutoIncrementIdAndNamesMapping> model;

    EXPECT_EQ(command.insert(model.getModelInfo()), insertSqlWithMappedAutoIncrementId);
}

TEST_F(DefaultInsertCommandTest, insertWithModelRelatedToAutoIncrementModel)
{
    orm::Model<models::ModelRelatedToAutoIncrementModel> model;

    EXPECT_EQ(command.insert(model.getModelInfo()), insertSqlWithModelRelatedToAutoIncrementModel);
}
