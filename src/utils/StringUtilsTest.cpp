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

TEST(StringUtilsTest, shouldNotReplaceWhenPatternIsEmpty)
{
    auto text = "text text"s;
    auto toReplace = ""s;
    auto replaceWith = "s"s;
    auto expected = "text text"s;

    replaceAll(text, toReplace, replaceWith);

    EXPECT_EQ(text, expected);
}

TEST(StringUtilsTest, shouldReplaceAllWithTheSameText)
{
    auto text = "text text"s;
    auto toReplace = "text"s;
    auto replaceWith = "text"s;
    auto expected = "text text"s;

    replaceAll(text, toReplace, replaceWith);

    EXPECT_EQ(text, expected);
}

TEST(StringUtilsTest, shouldRemoveAllMatchesWhenReplacementIsEmpty)
{
    auto text = "text text"s;
    auto toReplace = "t"s;
    auto replaceWith = ""s;
    auto expected = "ex ex"s;

    replaceAll(text, toReplace, replaceWith);

    EXPECT_EQ(text, expected);
}

TEST(StringUtilsTest, shouldRemoveLastComma)
{
    auto text = "text text,"s;
    auto expected = "text text"s;

    removeLastComma(text);

    EXPECT_EQ(text, expected);
}

TEST(StringUtilsTest, shouldRemoveSingleComma)
{
    auto text = ","s;
    auto expected = ""s;

    removeLastComma(text);

    EXPECT_EQ(text, expected);
}

TEST(StringUtilsTest, shouldNotRemoveCommaWhenSingleCharacterStringDoesNotContainComma)
{
    auto text = "a"s;
    auto expected = "a"s;

    removeLastComma(text);

    EXPECT_EQ(text, expected);
}

TEST(StringUtilsTest, shouldRemoveCommaFromTheBeginning)
{
    auto text = ", text"s;
    auto expected = ""s;

    removeLastComma(text);

    EXPECT_EQ(text, expected);
}

TEST(StringUtilsTest, shouldNotRemoveLastComma)
{
    auto text = "text text"s;
    auto expected = "text text"s;

    removeLastComma(text);

    EXPECT_EQ(text, expected);
}

TEST(StringUtilsTest, shouldNotRemoveLastCommaFromEmptyString)
{
    auto text = ""s;
    auto expected = ""s;

    removeLastComma(text);

    EXPECT_EQ(text, expected);
}

TEST(StringUtilsTest, shouldRemoveCommanIfFollowedBySpace)
{
    auto text = "text text, "s;
    auto expected = "text text"s;

    removeLastComma(text);

    EXPECT_EQ(text, expected);
}
