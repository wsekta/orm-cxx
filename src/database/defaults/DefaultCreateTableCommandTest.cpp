#include "DefaultCreateTableCommand.hpp"

#include <gtest/gtest.h>

#include "../sqlite/SqliteTypeTranslator.hpp"
#include "orm-cxx/model.hpp"
#include "tests/ModelsDefinitions.hpp"

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

const std::string createTableSqlWithReferringToCompositeIdModel =
    "CREATE TABLE IF NOT EXISTS models_ModelRelatedToCompositeIdModel (\n"
    "\tid INTEGER NOT NULL,\n"
    "\tfield1 TEXT NOT NULL,\n"
    "\tfield3_field1 INTEGER NOT NULL,\n"
    "\tfield3_field2 TEXT NOT NULL,\n"
    "\tPRIMARY KEY (id),\n"
    "\tFOREIGN KEY (field3_field1, field3_field2) REFERENCES models_ModelWithOverwrittenId (field1, field2)\n"
    ");";

const std::string createTableSqlWithAutoIncrementId =
    "CREATE TABLE IF NOT EXISTS models_ModelWithAutoIncrementId (\n"
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
} // namespace

class DefaultCreateTableCommandTest : public ::testing::Test
{
public:
    std::shared_ptr<orm::db::TypeTranslator> typeTranslator = std::make_shared<orm::db::sqlite::SqliteTypeTranslator>();

    orm::db::commands::DefaultCreateTableCommand command{typeTranslator};

    orm::Model<models::ModelWithFloat> model;
};

TEST_F(DefaultCreateTableCommandTest, createTable)
{
    EXPECT_EQ(command.createTable(model.getModelInfo()), createTableSql);
}

TEST_F(DefaultCreateTableCommandTest, createTableWithReferringToSimpleModel)
{
    orm::Model<models::ModelRelatedToOtherModel> model;

    EXPECT_EQ(command.createTable(model.getModelInfo()), createTableSqlWithReferringToSimpleModel);
}

TEST_F(DefaultCreateTableCommandTest, createTableWithReferringToCompositeIdModel)
{
    orm::Model<models::ModelRelatedToCompositeIdModel> model;

    EXPECT_EQ(command.createTable(model.getModelInfo()), createTableSqlWithReferringToCompositeIdModel);
}

TEST_F(DefaultCreateTableCommandTest, createTableWithAutoIncrementId)
{
    orm::Model<models::ModelWithAutoIncrementId> model;

    EXPECT_EQ(command.createTable(model.getModelInfo()), createTableSqlWithAutoIncrementId);
}

TEST_F(DefaultCreateTableCommandTest, createTableWithMappedAutoIncrementId)
{
    orm::Model<models::ModelWithAutoIncrementIdAndNamesMapping> model;

    EXPECT_EQ(command.createTable(model.getModelInfo()), createTableSqlWithMappedAutoIncrementId);
}

TEST_F(DefaultCreateTableCommandTest, createTableWithReferringToAutoIncrementModel)
{
    orm::Model<models::ModelRelatedToAutoIncrementModel> model;

    EXPECT_EQ(command.createTable(model.getModelInfo()), createTableSqlWithReferringToAutoIncrementModel);
}
