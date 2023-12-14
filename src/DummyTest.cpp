#include "orm-cxx/Dummy.hpp"

#include <gtest/gtest.h>

TEST(DummyTest, AlwaysPasses)
{
    dummy();
    EXPECT_EQ(1, 1);
}