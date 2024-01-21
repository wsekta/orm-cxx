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
