#include "DefaultDropTableCommand.hpp"

#include <gtest/gtest.h>

#include "orm-cxx/model.hpp"
#include "tests/ModelsDefinitions.hpp"
#include "tests/utils/SqlDialectTestDoubles.hpp"

namespace
{
const std::string dropTableSql = "DROP TABLE IF EXISTS models_ModelWithFloat;";
} // namespace

class DefaultDropTableCommandTest : public ::testing::Test
{
public:
    orm::tests::SnapshotSqliteDialect dialect;
    orm::db::commands::DefaultDropTableCommand command{dialect};

    orm::Model<models::ModelWithFloat> model;
};

TEST_F(DefaultDropTableCommandTest, dropTable)
{
    EXPECT_EQ(command.dropTable(model.getModelInfo()), dropTableSql);
}

TEST(DefaultDropTableCommandDialectTest, delegatesDropSyntaxAndIdentifierToDialect)
{
    orm::tests::TrackingSqlDialect dialect;
    orm::db::commands::DefaultDropTableCommand command{dialect};
    const orm::Model<models::ModelWithFloat> model;

    EXPECT_EQ(command.dropTable(model.getModelInfo()), "DROP_PORTABLE_IF_PRESENT [models_ModelWithFloat];");
}
