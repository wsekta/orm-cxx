#include "orm-cxx/model.hpp"

#include <gtest/gtest.h>

struct OneFieldStruct
{
    int field1;
};

struct StructWithTableName
{
    static constexpr std::string_view table_name = "some_table_name";

    int field1;
    int field2;
};

struct StructWithOptional
{
    std::optional<int> field1;
    std::optional<std::string> field2;
};

struct StructWithId
{
    int id;
    int field1;
    int field2;
};

struct StructWithIdColumns
{
    inline static const std::vector<std::string> id_columns = {"id1", "id2"};

    int id1;
    int id2;
    int field1;
    int field2;
};

TEST(ModelTest, OneFieldStruct_shouldHaveOneColumn)
{
    orm::Model<OneFieldStruct> model;

    EXPECT_EQ(model.getModelInfo().columnsInfo.size(), 1);
    EXPECT_EQ(model.getModelInfo().columnsInfo[0].name, "field1");
    EXPECT_EQ(model.getModelInfo().columnsInfo[0].type, orm::model::ColumnType::Int);
    EXPECT_EQ(model.getModelInfo().columnsInfo[0].isNotNull, true);
    EXPECT_TRUE(model.getModelInfo().idColumnsNames.empty());
}

TEST(ModelTest, StructWithTableName_shouldHaveTwoColumns)
{
    orm::Model<StructWithTableName> model;

    EXPECT_EQ(model.getModelInfo().columnsInfo.size(), 2);
    EXPECT_EQ(model.getModelInfo().columnsInfo[0].name, "field1");
    EXPECT_EQ(model.getModelInfo().columnsInfo[1].name, "field2");
    EXPECT_EQ(model.getModelInfo().tableName, StructWithTableName::table_name);
    EXPECT_TRUE(model.getModelInfo().idColumnsNames.empty());
}

TEST(ModelTest, StructWithOptional_shouldHaveTwoColumns)
{
    orm::Model<StructWithOptional> model;

    EXPECT_EQ(model.getModelInfo().columnsInfo.size(), 2);
    EXPECT_EQ(model.getModelInfo().columnsInfo[0].name, "field1");
    EXPECT_EQ(model.getModelInfo().columnsInfo[0].type, orm::model::ColumnType::Int);
    EXPECT_EQ(model.getModelInfo().columnsInfo[0].isNotNull, false);
    EXPECT_EQ(model.getModelInfo().columnsInfo[1].name, "field2");
    EXPECT_EQ(model.getModelInfo().columnsInfo[1].isNotNull, false);
    EXPECT_TRUE(model.getModelInfo().idColumnsNames.empty());
}

TEST(ModelTest, StructWithId_shouldHaveOneIdColumn)
{
    orm::Model<StructWithId> model;

    EXPECT_EQ(model.getModelInfo().columnsInfo.size(), 3);
    EXPECT_EQ(model.getModelInfo().columnsInfo[0].name, "id");
    EXPECT_EQ(model.getModelInfo().columnsInfo[0].isPrimaryKey, true);
    EXPECT_EQ(model.getModelInfo().columnsInfo[1].name, "field1");
    EXPECT_EQ(model.getModelInfo().columnsInfo[1].isPrimaryKey, false);
    EXPECT_EQ(model.getModelInfo().columnsInfo[2].name, "field2");
    EXPECT_EQ(model.getModelInfo().columnsInfo[2].isPrimaryKey, false);
    EXPECT_TRUE(model.getModelInfo().idColumnsNames.contains("id"));
}

TEST(ModelTest, StructWithIdColumns_shouldHaveTwoIdColumns)
{
    orm::Model<StructWithIdColumns> model;

    EXPECT_EQ(model.getModelInfo().columnsInfo.size(), 4);
    EXPECT_EQ(model.getModelInfo().columnsInfo[0].name, "id1");
    EXPECT_EQ(model.getModelInfo().columnsInfo[0].isPrimaryKey, true);
    EXPECT_EQ(model.getModelInfo().columnsInfo[1].name, "id2");
    EXPECT_EQ(model.getModelInfo().columnsInfo[1].isPrimaryKey, true);
    EXPECT_EQ(model.getModelInfo().columnsInfo[2].name, "field1");
    EXPECT_EQ(model.getModelInfo().columnsInfo[2].isPrimaryKey, false);
    EXPECT_EQ(model.getModelInfo().columnsInfo[3].name, "field2");
    EXPECT_EQ(model.getModelInfo().columnsInfo[3].isPrimaryKey, false);
    EXPECT_TRUE(model.getModelInfo().idColumnsNames.contains("id1"));
    EXPECT_TRUE(model.getModelInfo().idColumnsNames.contains("id2"));
}