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

TEST(ModelTest, OneFieldStruct_shouldHaveOneColumn)
{
    orm::Model<OneFieldStruct> model;

    EXPECT_EQ(model.getColumnsInfo().size(), 1);
    EXPECT_EQ(model.getColumnsInfo()[0].name, "field1");
    EXPECT_EQ(model.getColumnsInfo()[0].type, "int");
    EXPECT_EQ(model.getColumnsInfo()[0].isNotNull, true);
}

TEST(ModelTest, StructWithTableName_shouldHaveTwoColumns)
{
    orm::Model<StructWithTableName> model;

    EXPECT_EQ(model.getColumnsInfo().size(), 2);
    EXPECT_EQ(model.getColumnsInfo()[0].name, "field1");
    EXPECT_EQ(model.getColumnsInfo()[1].name, "field2");
    EXPECT_EQ(model.getTableName(), StructWithTableName::table_name);
}

TEST(ModelTest, StructWithOptional_shouldHaveTwoColumns)
{
    orm::Model<StructWithOptional> model;

    EXPECT_EQ(model.getColumnsInfo().size(), 2);
    EXPECT_EQ(model.getColumnsInfo()[0].name, "field1");
    EXPECT_EQ(model.getColumnsInfo()[0].type, "int");
    EXPECT_EQ(model.getColumnsInfo()[0].isNotNull, false);
    EXPECT_EQ(model.getColumnsInfo()[1].name, "field2");
    EXPECT_EQ(model.getColumnsInfo()[1].isNotNull, false);
}
