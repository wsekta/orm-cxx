#include "DefaultInsertCommand.hpp"

#include <gtest/gtest.h>
#include <string_view>
#include <vector>

#include "orm-cxx/model.hpp"
#include "tests/ModelsDefinitions.hpp"
#include "tests/utils/SqlDialectTestDoubles.hpp"

namespace default_insert_command_models
{
struct AutoOnlyModel
{
    inline static constexpr std::string_view table_name = "auto_only";
    inline static const std::vector<std::string> auto_increment_columns = {"id"};

    int id;
};
} // namespace default_insert_command_models

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
    orm::tests::SnapshotSqliteDialect dialect;
    orm::db::commands::DefaultInsertCommand command{dialect};

    orm::Model<models::ModelWithFloat> model;
};

TEST_F(DefaultInsertCommandTest, insert)
{
    EXPECT_EQ(command.insert(model.getModelInfo()), insertSql);
}

TEST(DefaultInsertCommandDialectTest, delegatesIdentifiersAndBindMarkersToDialect)
{
    orm::tests::TrackingSqlDialect dialect;
    orm::db::commands::DefaultInsertCommand command{dialect};
    const orm::Model<models::ModelWithFloat> model;

    EXPECT_EQ(command.insert(model.getModelInfo()),
              "INSERT INTO [models_ModelWithFloat] ([field1], [field2], [field3]) "
              "VALUES ($field1, $field2, $field3);");
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

TEST_F(DefaultInsertCommandTest, insertWithOnlyAutoIncrementIdUsesDefaultValues)
{
    const orm::Model<default_insert_command_models::AutoOnlyModel> model;

    EXPECT_EQ(command.insert(model.getModelInfo()), "INSERT INTO auto_only DEFAULT VALUES;");
}
