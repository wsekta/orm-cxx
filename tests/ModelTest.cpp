#include "orm-cxx/model.hpp"

#include <gtest/gtest.h>

namespace
{
struct ZeroFieldStruct
{
    /* zero */
};

struct OneFieldStruct
{
    int field1;
};
}

TEST(ModelTest, ZeroFieldStruct_shouldHaveZeroColumn)
{
    orm::Model<ZeroFieldStruct> model;

    EXPECT_EQ(model.getColumnNames().size(), 0);
}

// TODO: fix this test
// TEST(ModelTest, OneFieldStruct_shouldHaveOneColumn)
// {
//     orm::Model<OneFieldStruct> model;

//     EXPECT_EQ(model.getColumnNames().size(), 1);
// }
