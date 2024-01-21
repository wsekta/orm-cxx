#include "DefaultDropTableCommand.hpp"

#include <gtest/gtest.h>

#include "orm-cxx/model.hpp"
#include "tests/ModelsDefinitions.hpp"

namespace
{
const std::string dropTableSql = "DROP TABLE IF EXISTS models_ModelWithFloat;";
} // namespace

class DefaultDropTableCommandTest : public ::testing::Test
{
public:
    orm::db::commands::DefaultDropTableCommand command;

    orm::Model<models::ModelWithFloat> model;
};

TEST_F(DefaultDropTableCommandTest, dropTable)
{
    EXPECT_EQ(command.dropTable(model.getModelInfo()), dropTableSql);
}
