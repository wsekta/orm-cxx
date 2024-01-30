#include "orm-cxx/model/ColumnType.hpp"

#include <gtest/gtest.h>

using namespace orm::model;

namespace
{
const std::string charType{"char"};
const std::string unsignedCharType{"unsigned char"};
const std::string shortType{"short"};
const std::string unsignedShortType{"unsigned short"};
const std::string longType{"long"};
const std::string unsignedLongType{"unsigned long"};
const std::string longLongType{"long long"};
const std::string unsignedLongLongType{"unsigned long long"};
const std::string boolType{"bool"};
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

TEST(ColumnTypeTests, shouldTranslateChar)
{
    auto [columnType, isNotNull] = toColumnType(charType);
    EXPECT_EQ(columnType, ColumnType::Char);
    EXPECT_TRUE(isNotNull);
}

TEST(ColumnTypeTests, shouldTranslateUnsignedChar)
{
    auto [columnType, isNotNull] = toColumnType(unsignedCharType);
    EXPECT_EQ(columnType, ColumnType::UnsignedChar);
    EXPECT_TRUE(isNotNull);
}

TEST(ColumnTypeTests, shouldTranslateShort)
{
    auto [columnType, isNotNull] = toColumnType(shortType);
    EXPECT_EQ(columnType, ColumnType::Short);
    EXPECT_TRUE(isNotNull);
}

TEST(ColumnTypeTests, shouldTranslateUnsignedShort)
{
    auto [columnType, isNotNull] = toColumnType(unsignedShortType);
    EXPECT_EQ(columnType, ColumnType::UnsignedShort);
    EXPECT_TRUE(isNotNull);
}

TEST(ColumnTypeTests, shouldTranslateLongLong)
{
    auto [columnType, isNotNull] = toColumnType(longLongType);
    EXPECT_EQ(columnType, ColumnType::LongLong);
    EXPECT_TRUE(isNotNull);
}

TEST(ColumnTypeTests, shouldTranslateLong)
{
    auto [columnType, isNotNull] = toColumnType(longType);
    EXPECT_EQ(columnType, ColumnType::Int);
    EXPECT_TRUE(isNotNull);
}

TEST(ColumnTypeTests, shouldTranslateUnsignedLong)
{
    auto [columnType, isNotNull] = toColumnType(unsignedLongType);
    EXPECT_EQ(columnType, ColumnType::UnsignedInt);
    EXPECT_TRUE(isNotNull);
}

TEST(ColumnTypeTests, shouldTranslateUnsignedLongLong)
{
    auto [columnType, isNotNull] = toColumnType(unsignedLongLongType);
    EXPECT_EQ(columnType, ColumnType::UnsignedLongLong);
    EXPECT_TRUE(isNotNull);
}

TEST(ColumnTypeTests, shouldTranslateBool)
{
    auto [columnType, isNotNull] = toColumnType(boolType);
    EXPECT_EQ(columnType, ColumnType::Bool);
    EXPECT_TRUE(isNotNull);
}

