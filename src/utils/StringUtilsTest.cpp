#include "orm-cxx/utils/StringUtils.hpp"

#include <gtest/gtest.h>

using namespace orm::utils;
using namespace std::string_literals;

TEST(StringUtilsTest, shouldReplaceAll)
{
    auto text = "text text"s;
    auto toReplace = "x"s;
    auto replaceWith = "s"s;
    auto expected = "test test"s;

    replaceAll(text, toReplace, replaceWith);

    EXPECT_EQ(text, expected);
}

TEST(StringUtilsTest, shouldNotReplaceAll)
{
    auto text = "text text"s;
    auto toReplace = "s"s;
    auto replaceWith = "s"s;
    auto expected = "text text"s;
    replaceAll(text, toReplace, replaceWith);

    EXPECT_EQ(text, expected);
}
