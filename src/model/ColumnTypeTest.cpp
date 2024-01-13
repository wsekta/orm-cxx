#include "orm-cxx/model/ColumnType.hpp"

#include <gtest/gtest.h>

using namespace orm::model;

namespace
{
std::string intType{"int"};
std::string floatType{"float"};
std::string doubleType{"double"};
std::string stringTypeVisualStudioStyle{
    "class std::basic_string<char,struct std::char_traits<char>,class std::allocator<char> >"};
std::string stringTypeClangStyle{"std::basic_string<char, std::char_traits<char>, std::allocator<char>>"};
std::string stringTypeGxxStyle{"std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >"};
std::string optionalIntTypeVisualStudioStyle{"class std::optional<int>"};
std::string optionalIntTypeClangStyle{"std::optional<int>"};
std::string optionalStringTypeVisualStudioStyle{
    "class std::optional<class std::basic_string<char,struct std::char_traits<char>,class std::allocator<char> > >"};
std::string optionalStringTypeClangStyle{
    "std::optional<std::basic_string<char, std::char_traits<char>, std::allocator<char>>>"};
}

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