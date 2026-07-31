#include "DefaultInsertCommand.hpp"

#include <gtest/gtest.h>

#include "tests/ModelsDefinitions.hpp"
#include "tests/utils/SqlDialectTestDoubles.hpp"

namespace default_insert_command_models
{
struct AutoOnlyModel
{
    int id;

    inline static constexpr orm::reflection::FixedString table_name{"auto_only"};
    inline static constexpr auto auto_increment_columns = orm::autoIncrement<&AutoOnlyModel::id>();
};

using Schema = orm::Schema<AutoOnlyModel>;
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

template <typename T>
constexpr auto modelView() -> orm::model::ModelView
{
    return orm::modelView<models::Schema, T>();
}
} // namespace

class DefaultInsertCommandTest : public ::testing::Test
{
public:
    orm::tests::SnapshotSqliteDialect dialect;
    orm::db::commands::DefaultInsertCommand command{dialect};
};

TEST_F(DefaultInsertCommandTest, insert)
{
    EXPECT_EQ(command.insert(modelView<models::ModelWithFloat>()), insertSql);
}

TEST(DefaultInsertCommandDialectTest, delegatesIdentifiersAndBindMarkersToDialect)
{
    orm::tests::TrackingSqlDialect dialect;
    orm::db::commands::DefaultInsertCommand command{dialect};
    EXPECT_EQ(command.insert(modelView<models::ModelWithFloat>()),
              "INSERT INTO [models_ModelWithFloat] ([field1], [field2], [field3]) "
              "VALUES ($field1, $field2, $field3);");
}

TEST_F(DefaultInsertCommandTest, insertWithModelRelatedToOtherModel)
{
    EXPECT_EQ(command.insert(modelView<models::ModelRelatedToOtherModel>()), insertSqlWithModelRelatedToOtherModel);
}

TEST_F(DefaultInsertCommandTest, insertWithModelRelatedToCompositeIdModel)
{
    EXPECT_EQ(command.insert(modelView<models::ModelRelatedToCompositeIdModel>()),
              insertSqlWithModelRelatedToCompositeIdModel);
}

TEST_F(DefaultInsertCommandTest, insertWithAutoIncrementId)
{
    EXPECT_EQ(command.insert(modelView<models::ModelWithAutoIncrementId>()), insertSqlWithAutoIncrementId);
}

TEST_F(DefaultInsertCommandTest, insertWithMappedAutoIncrementId)
{
    EXPECT_EQ(command.insert(modelView<models::ModelWithAutoIncrementIdAndNamesMapping>()),
              insertSqlWithMappedAutoIncrementId);
}

TEST_F(DefaultInsertCommandTest, insertWithModelRelatedToAutoIncrementModel)
{
    EXPECT_EQ(command.insert(modelView<models::ModelRelatedToAutoIncrementModel>()),
              insertSqlWithModelRelatedToAutoIncrementModel);
}

TEST_F(DefaultInsertCommandTest, insertWithOnlyAutoIncrementIdUsesDefaultValues)
{
    EXPECT_EQ(
        command.insert(
            orm::modelView<default_insert_command_models::Schema, default_insert_command_models::AutoOnlyModel>()),
        "INSERT INTO auto_only DEFAULT VALUES;");
}
