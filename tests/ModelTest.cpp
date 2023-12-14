#include "orm-cxx/model.hpp"

#include <gtest/gtest.h>

#include "faker-cxx/Number.h"

struct ZeroFieldStruct
{
    /* zero */
};

struct OneFieldStruct
{
    int field1 = faker::Number::integer(100);
};

TEST(ModelTest, ZeroFieldStruct_shouldHaveZeroColumn)
{
    orm::Model<ZeroFieldStruct> model;

    EXPECT_EQ(model.getColumnNames().size(), 0);
}

TEST(ModelTest, OneFieldStruct_shouldHaveOneColumn)
{
    orm::Model<OneFieldStruct> model;

    EXPECT_EQ(model.getColumnNames().size(), 1);
    EXPECT_EQ(model.getColumnNames()[0], "field1");
}
