#include "DefaultInsertCommand.hpp"

#include <gtest/gtest.h>

#include "orm-cxx/model.hpp"
#include "tests/ModelsDefinitions.hpp"

namespace
{
const std::string insertSql =
    "INSERT INTO models_ModelWithFloat (field1, field2, field3) VALUES (:field1, :field2, :field3);";
const std::string insertSqlWithModelRelatedToOtherModel =
    "INSERT INTO models_ModelRelatedToOtherModel (id, field1, field2, field3_id) VALUES (:id, :field1, :field2, :field3_id);";
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