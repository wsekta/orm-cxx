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
