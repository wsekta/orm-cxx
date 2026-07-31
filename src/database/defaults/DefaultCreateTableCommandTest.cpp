#include "DefaultCreateTableCommand.hpp"

#include <gtest/gtest.h>

#include "tests/ModelsDefinitions.hpp"
#include "tests/utils/SqlDialectTestDoubles.hpp"

namespace
{
const std::string createTableSql = "CREATE TABLE IF NOT EXISTS models_ModelWithFloat (\n"
                                   "\tfield1 INTEGER NOT NULL,\n"
                                   "\tfield2 TEXT NOT NULL,\n"
                                   "\tfield3 REAL NOT NULL\n"
                                   ");";

const std::string createTableSqlWithReferringToSimpleModel = "CREATE TABLE IF NOT EXISTS "
                                                             "models_ModelRelatedToOtherModel (\n"
                                                             "\tid INTEGER NOT NULL,\n"
                                                             "\tfield1 INTEGER NOT NULL,\n"
                                                             "\tfield2 TEXT NOT NULL,\n"
                                                             "\tfield3_id INTEGER NOT NULL,\n"
                                                             "\tPRIMARY KEY (id),\n"
                                                             "\tFOREIGN KEY (field3_id) REFERENCES "
                                                             "models_ModelWithId (id)\n"
                                                             ");";

const std::string createTableSqlWithOptionalFields = "CREATE TABLE IF NOT EXISTS models_ModelWithOptional (\n"
                                                     "\tfield1 INTEGER,\n"
                                                     "\tfield2 TEXT,\n"
                                                     "\tfield3 REAL\n"
                                                     ");";

const std::string createTableSqlWithOptionalRelation = "CREATE TABLE IF NOT EXISTS "
                                                       "models_ModelOptionallyRelatedToOtherModel (\n"
                                                       "\tid INTEGER NOT NULL,\n"
                                                       "\tfield1 INTEGER NOT NULL,\n"
                                                       "\tfield2 TEXT NOT NULL,\n"
                                                       "\tfield3_id INTEGER,\n"
                                                       "\tPRIMARY KEY (id),\n"
                                                       "\tFOREIGN KEY (field3_id) REFERENCES "
                                                       "models_ModelWithId (id)\n"
                                                       ");";

const std::string createTableSqlWithReferringToCompositeIdModel =
    "CREATE TABLE IF NOT EXISTS models_ModelRelatedToCompositeIdModel (\n"
    "\tid INTEGER NOT NULL,\n"
    "\tfield1 TEXT NOT NULL,\n"
    "\tfield3_field1 INTEGER NOT NULL,\n"
    "\tfield3_field2 TEXT NOT NULL,\n"
    "\tPRIMARY KEY (id),\n"
    "\tFOREIGN KEY (field3_field1, field3_field2) REFERENCES models_ModelWithOverwrittenId (field1, field2)\n"
    ");";

const std::string createTableSqlWithAutoIncrementId = "CREATE TABLE IF NOT EXISTS models_ModelWithAutoIncrementId (\n"
                                                      "\tid INTEGER PRIMARY KEY AUTOINCREMENT,\n"
                                                      "\tfield1 INTEGER NOT NULL,\n"
                                                      "\tfield2 TEXT NOT NULL\n"
                                                      ");";

const std::string createTableSqlWithMappedAutoIncrementId =
    "CREATE TABLE IF NOT EXISTS models_ModelWithAutoIncrementIdAndNamesMapping (\n"
    "\tsome_id_name INTEGER PRIMARY KEY AUTOINCREMENT,\n"
    "\tsome_field1_name INTEGER NOT NULL,\n"
    "\tsome_field2_name TEXT NOT NULL\n"
    ");";

const std::string createTableSqlWithReferringToAutoIncrementModel =
    "CREATE TABLE IF NOT EXISTS models_ModelRelatedToAutoIncrementModel (\n"
    "\tid INTEGER NOT NULL,\n"
    "\tfield1 INTEGER NOT NULL,\n"
    "\tfield3_id INTEGER NOT NULL,\n"
    "\tPRIMARY KEY (id),\n"
    "\tFOREIGN KEY (field3_id) REFERENCES models_ModelWithAutoIncrementId (id)\n"
    ");";

template <typename T>
constexpr auto modelView() -> orm::model::ModelView
{
    return orm::modelView<models::Schema, T>();
}
} // namespace

class DefaultCreateTableCommandTest : public ::testing::Test
{
public:
    orm::tests::SnapshotSqliteDialect dialect;
    orm::db::commands::DefaultCreateTableCommand command{dialect};
};

TEST_F(DefaultCreateTableCommandTest, createTable)
{
    EXPECT_EQ(command.createTable(modelView<models::ModelWithFloat>()), createTableSql);
}

TEST_F(DefaultCreateTableCommandTest, createTableWithReferringToSimpleModel)
{
    EXPECT_EQ(command.createTable(modelView<models::ModelRelatedToOtherModel>()),
              createTableSqlWithReferringToSimpleModel);
}

TEST_F(DefaultCreateTableCommandTest, createTableWithOptionalFields)
{
    EXPECT_EQ(command.createTable(modelView<models::ModelWithOptional>()), createTableSqlWithOptionalFields);
}

TEST_F(DefaultCreateTableCommandTest, createTableWithOptionalRelation)
{
    EXPECT_EQ(command.createTable(modelView<models::ModelOptionallyRelatedToOtherModel>()),
              createTableSqlWithOptionalRelation);
}

TEST_F(DefaultCreateTableCommandTest, createTableWithReferringToCompositeIdModel)
{
    EXPECT_EQ(command.createTable(modelView<models::ModelRelatedToCompositeIdModel>()),
              createTableSqlWithReferringToCompositeIdModel);
}

TEST_F(DefaultCreateTableCommandTest, createTableWithAutoIncrementId)
{
    EXPECT_EQ(command.createTable(modelView<models::ModelWithAutoIncrementId>()), createTableSqlWithAutoIncrementId);
}

TEST_F(DefaultCreateTableCommandTest, createTableWithMappedAutoIncrementId)
{
    EXPECT_EQ(command.createTable(modelView<models::ModelWithAutoIncrementIdAndNamesMapping>()),
              createTableSqlWithMappedAutoIncrementId);
}

TEST_F(DefaultCreateTableCommandTest, createTableWithReferringToAutoIncrementModel)
{
    EXPECT_EQ(command.createTable(modelView<models::ModelRelatedToAutoIncrementModel>()),
              createTableSqlWithReferringToAutoIncrementModel);
}

TEST(DefaultCreateTableCommandDialectTest, delegatesDdlTypesAutoincrementAndIdentifiersToDialect)
{
    orm::tests::TrackingSqlDialect dialect;
    orm::db::commands::DefaultCreateTableCommand command{dialect};
    EXPECT_EQ(command.createTable(modelView<models::ModelWithAutoIncrementId>()),
              "CREATE_PORTABLE_IF_ABSENT [models_ModelWithAutoIncrementId] (\n"
              "\tPORTABLE_AUTO [id],\n"
              "\t[field1] PORTABLE_TYPE NOT NULL,\n"
              "\t[field2] PORTABLE_TYPE NOT NULL\n"
              ");");
}
