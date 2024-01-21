#include "DefaultInsertCommand.hpp"

#include <gtest/gtest.h>

#include "orm-cxx/model.hpp"
#include "tests/ModelsDefinitions.hpp"

namespace
{
const std::string insertSql =
    "INSERT INTO models_ModelWithFloat (field1, field2, field3) VALUES (:field1, :field2, :field3);";
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
