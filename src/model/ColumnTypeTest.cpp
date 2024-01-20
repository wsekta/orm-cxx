#include "orm-cxx/model/ColumnType.hpp"

#include <gtest/gtest.h>

using namespace orm::model;

namespace
{
const std::string intType{"int"};
const std::string floatType{"float"};
const std::string doubleType{"double"};
const std::string stringTypeVisualStudioStyle{
    "class std::basic_string<char,struct std::char_traits<char>,class std::allocator<char> >"};
const std::string stringTypeClangStyle{"std::basic_string<char, std::char_traits<char>, std::allocator<char>>"};
const std::string stringTypeGxxStyle{"std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >"};
const std::string optionalIntTypeVisualStudioStyle{"class std::optional<int>"};
const std::string optionalIntTypeClangStyle{"std::optional<int>"};
const std::string optionalStringTypeVisualStudioStyle{
    "class std::optional<class std::basic_string<char,struct std::char_traits<char>,class std::allocator<char> > >"};
const std::string optionalStringTypeClangStyle{
    "std::optional<std::basic_string<char, std::char_traits<char>, std::allocator<char>>>"};
} // namespace

TEST(ColumnTypeTests, shouldTranslateInt)
{
    auto [columnType, isNotNull] = toColumnType(intType);
    EXPECT_EQ(columnType, ColumnType::Int);
    EXPECT_TRUE(isNotNull);
}

TEST(ColumnTypeTests, shouldTranslateFloat)
{
    auto [columnType, isNotNull] = toColumnType(floatType);
    EXPECT_EQ(columnType, ColumnType::Float);
    EXPECT_TRUE(isNotNull);
}

TEST(ColumnTypeTests, shouldTranslateDouble)
{
    auto [columnType, isNotNull] = toColumnType(doubleType);
    EXPECT_EQ(columnType, ColumnType::Double);
    EXPECT_TRUE(isNotNull);
}

TEST(ColumnTypeTests, shouldTranslateStringVisualStudioStyle)
{
    auto [columnType, isNotNull] = toColumnType(stringTypeVisualStudioStyle);
    EXPECT_EQ(columnType, ColumnType::String);
    EXPECT_TRUE(isNotNull);
}

TEST(ColumnTypeTests, shouldTranslateStringClangStyle)
{
    auto [columnType, isNotNull] = toColumnType(stringTypeClangStyle);
    EXPECT_EQ(columnType, ColumnType::String);
    EXPECT_TRUE(isNotNull);
}

TEST(ColumnTypeTests, shouldTranslateStringGxxStyle)
{
    auto [columnType, isNotNull] = toColumnType(stringTypeGxxStyle);
    EXPECT_EQ(columnType, ColumnType::String);
    EXPECT_TRUE(isNotNull);
}

TEST(ColumnTypeTests, shouldTranslateOptionalIntVisualStudioStyle)
{
    auto [columnType, isNotNull] = toColumnType(optionalIntTypeVisualStudioStyle);
    EXPECT_EQ(columnType, ColumnType::Int);
    EXPECT_FALSE(isNotNull);
}

TEST(ColumnTypeTests, shouldTranslateOptionalIntClangStyle)
{
    auto [columnType, isNotNull] = toColumnType(optionalIntTypeClangStyle);
    EXPECT_EQ(columnType, ColumnType::Int);
    EXPECT_FALSE(isNotNull);
}

TEST(ColumnTypeTests, shouldTranslateOptionalStringVisualStudioStyle)
{
    auto [columnType, isNotNull] = toColumnType(optionalStringTypeVisualStudioStyle);
    EXPECT_EQ(columnType, ColumnType::String);
    EXPECT_FALSE(isNotNull);
}

TEST(ColumnTypeTests, shouldTranslateOptionalStringClangStyle)
{
    auto [columnType, isNotNull] = toColumnType(optionalStringTypeClangStyle);
    EXPECT_EQ(columnType, ColumnType::String);
    EXPECT_FALSE(isNotNull);
}

TEST(ColumnTypeTests, shouldTranslateUnknownType)
{
    auto [columnType, isNotNull] = toColumnType("unknown");
    EXPECT_EQ(columnType, ColumnType::Unknown);
    EXPECT_TRUE(isNotNull);
}
