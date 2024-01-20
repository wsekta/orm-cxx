#include "SqliteCommandGenerator.hpp"

#include <gtest/gtest.h>

#include "orm-cxx/model.hpp"
#include "orm-cxx/query.hpp"
#include "tests/ModelsDefinitions.hpp"

using namespace orm::db::sqlite;

namespace
{
const std::string createTableSql = "CREATE TABLE IF NOT EXISTS models_ModelWithFloat (\n"
                                   "\tfield1 INTEGER NOT NULL,\n"
                                   "\tfield2 TEXT NOT NULL,\n"
                                   "\tfield3 REAL NOT NULL\n"
                                   ");";
const std::string dropTableSql = "DROP TABLE IF EXISTS models_ModelWithFloat;";
const std::string insertSql =
    "INSERT INTO models_ModelWithFloat (field1, field2, field3) VALUES (:field1, :field2, :field3);";
const std::string createTableSqlWithOptional = "CREATE TABLE IF NOT EXISTS models_ModelWithOptional (\n"
                                               "\tfield1 INTEGER,\n"
                                               "\tfield2 TEXT,\n"
                                               "\tfield3 REAL\n"
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

const std::string selectSql = "SELECT * FROM models_ModelWithFloat;";

const std::string selectSqlWithLimit = "SELECT * FROM models_ModelWithFloat LIMIT 10;";

const std::string selectSqlWithOffset = "SELECT * FROM models_ModelWithFloat OFFSET 10;";
}

class SqliteCommandGeneratorTest : public ::testing::Test
{
public:
    SqliteCommandGenerator generator;

    orm::Model<models::ModelWithFloat> model;
};

TEST_F(SqliteCommandGeneratorTest, createTable)
{
    EXPECT_EQ(generator.createTable(model.getModelInfo()), createTableSql);
}

TEST_F(SqliteCommandGeneratorTest, dropTable)
{
    EXPECT_EQ(generator.dropTable(model.getModelInfo()), dropTableSql);
}

TEST_F(SqliteCommandGeneratorTest, insert)
{
    EXPECT_EQ(generator.insert(model.getModelInfo()), insertSql);
}

TEST_F(SqliteCommandGeneratorTest, createTableWithOptional)
{
    orm::Model<models::ModelWithOptional> modelWithOptional;

    EXPECT_EQ(generator.createTable(modelWithOptional.getModelInfo()), createTableSqlWithOptional);
}

TEST_F(SqliteCommandGeneratorTest, createTableWithReferringToSimpleModel)
{
    orm::Model<models::ModelRelatedToOtherModel> modelWithReferringToSimpleModel;

    EXPECT_EQ(generator.createTable(modelWithReferringToSimpleModel.getModelInfo()),
              createTableSqlWithReferringToSimpleModel);
}

TEST_F(SqliteCommandGeneratorTest, select)
{
    orm::query::QueryData queryData{.modelInfo = model.getModelInfo()};

    EXPECT_EQ(generator.select(queryData), selectSql);
}

TEST_F(SqliteCommandGeneratorTest, selectWithLimit)
{
    orm::query::QueryData queryData{.modelInfo = model.getModelInfo(), .limit = 10};

    EXPECT_EQ(generator.select(queryData), selectSqlWithLimit);
}

TEST_F(SqliteCommandGeneratorTest, selectWithOffset)
{
    orm::query::QueryData queryData{.modelInfo = model.getModelInfo(), .offset = 10};

    EXPECT_EQ(generator.select(queryData), selectSqlWithOffset);
}