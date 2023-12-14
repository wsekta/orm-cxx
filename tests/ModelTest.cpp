#include "orm-cxx/model.hpp"

#include <gtest/gtest.h>

#include "faker-cxx/Number.h"
#include "faker-cxx/String.h"

using std::string_view_literals::operator""sv;

struct ZeroFieldStruct
{
    /* zero */
};

struct OneFieldStruct
{
    int field1 = faker::Number::integer(100);
};

struct StructWithTableName
{
    static constexpr std::string_view table_name = "some_table_name";
    
    int field1 = faker::Number::integer(100);
    int field2 = faker::Number::integer(100);
};

TEST(ModelTest, ZeroFieldStruct_shouldHaveZeroColumn)
{
    orm::Model<ZeroFieldStruct> model;

    EXPECT_EQ(model.getColumnNames().size(), 0);
    // EXPECT_EQ(model.getTableName(), "ZeroFieldStruct"sv);
}

TEST(ModelTest, OneFieldStruct_shouldHaveOneColumn)
{
    orm::Model<OneFieldStruct> model;

    EXPECT_EQ(model.getColumnNames().size(), 1);
    EXPECT_EQ(model.getColumnNames()[0], "field1");
}

TEST(ModelTest, StructWithTableName_shouldHaveTwoColumns)
{
    orm::Model<StructWithTableName> model;

    EXPECT_EQ(model.getColumnNames().size(), 2);
    EXPECT_EQ(model.getColumnNames()[0], "field1");
    EXPECT_EQ(model.getColumnNames()[1], "field2");
    EXPECT_EQ(model.getTableName(), StructWithTableName::table_name);
}
