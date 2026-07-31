#include "DefaultDropTableCommand.hpp"

#include <gtest/gtest.h>

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
};

TEST_F(DefaultDropTableCommandTest, dropTable)
{
    EXPECT_EQ(command.dropTable(orm::modelView<models::Schema, models::ModelWithFloat>()), dropTableSql);
}

TEST(DefaultDropTableCommandDialectTest, delegatesDropSyntaxAndIdentifierToDialect)
{
    orm::tests::TrackingSqlDialect dialect;
    orm::db::commands::DefaultDropTableCommand command{dialect};
    EXPECT_EQ(command.dropTable(orm::modelView<models::Schema, models::ModelWithFloat>()),
              "DROP_PORTABLE_IF_PRESENT [models_ModelWithFloat];");
}
