#include "SqliteCommandGenerator.hpp"

#include <gtest/gtest.h>

#include "orm-cxx/model.hpp"

using namespace orm::db::sqlite;

namespace
{
const std::string createTableSql = "CREATE TABLE IF NOT EXISTS SimpleModel (\n"
                                   "\tid INTEGER NOT NULL,\n"
                                   "\tname TEXT NOT NULL,\n"
                                   "\tvalue REAL NOT NULL,\n"
                                   "\tPRIMARY KEY (id)\n"
                                   ");";

const std::string dropTableSql = "DROP TABLE IF EXISTS SimpleModel;";

const std::string insertSql = "INSERT INTO SimpleModel (id, name, value) VALUES (:id, :name, :value);";

const std::string createTableSqlWithOptional = "CREATE TABLE IF NOT EXISTS SimpleModelWithOptional (\n"
                                               "\tid INTEGER NOT NULL,\n"
                                               "\tname TEXT,\n"
                                               "\tvalue REAL,\n"
                                               "\tPRIMARY KEY (id)\n"
                                               ");";

const std::string createTableSqlWithReferringToSimpleModel = "CREATE TABLE IF NOT EXISTS "
                                                             "ModelReferringToSimpleModel (\n"
                                                             "\tid INTEGER NOT NULL,\n"
                                                             "\tsimpleModel_id INTEGER NOT NULL,\n"
                                                             "\tPRIMARY KEY (id),\n"
                                                             "\tFOREIGN KEY (simpleModel_id) REFERENCES "
                                                             "SimpleModel (id)\n"
                                                             ");";
}

struct SimpleModel
{
    int id;
    std::string name;
    double value;
};

struct SimpleModelWithOptional
{
    int id;
    std::optional<std::string> name;
    std::optional<double> value;
};

struct ModelReferringToSimpleModel
{
    int id;
    SimpleModel simpleModel;
};

class SqliteCommandGeneratorTest : public ::testing::Test
{
public:
    SqliteCommandGenerator generator;

    orm::Model<SimpleModel> model;
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
    orm::Model<SimpleModelWithOptional> modelWithOptional;

    EXPECT_EQ(generator.createTable(modelWithOptional.getModelInfo()), createTableSqlWithOptional);
}

TEST_F(SqliteCommandGeneratorTest, createTableWithReferringToSimpleModel)
{
    orm::Model<ModelReferringToSimpleModel> modelWithReferringToSimpleModel;

    EXPECT_EQ(generator.createTable(modelWithReferringToSimpleModel.getModelInfo()),
              createTableSqlWithReferringToSimpleModel);
}