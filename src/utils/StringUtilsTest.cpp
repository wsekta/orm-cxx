#include "orm-cxx/utils/StringUtils.hpp"

#include <gtest/gtest.h>

using namespace orm::utils;
using namespace std::string_literals;

TEST(StringUtilsTest, shouldReplaceAll)
{
    auto s = "abcabc"s;
    auto toReplace = "a"s;
    auto replaceWith = "d"s;
    auto expected = "dbcdbc"s;

    replaceAll(s, toReplace, replaceWith);

    EXPECT_EQ(s, expected);
}

TEST(StringUtilsTest, shouldNotReplaceAll)
{
    auto s = "abcabc"s;
    auto toReplace = "d"s;
    auto replaceWith = "d"s;
    auto expected = "abcabc"s;

    replaceAll(s, toReplace, replaceWith);

    EXPECT_EQ(s, expected);
}