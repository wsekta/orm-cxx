#include <gtest/gtest.h>
#include <optional>
#include <string>

#include "orm-cxx/model/Mapping.hpp"

using namespace orm::model;

static_assert(logicalType<bool>() == ColumnType::Bool);
static_assert(logicalType<char>() == ColumnType::Char);
static_assert(logicalType<signed char>() == ColumnType::Char);
static_assert(logicalType<unsigned char>() == ColumnType::UnsignedChar);
static_assert(logicalType<short>() == ColumnType::Short);
static_assert(logicalType<unsigned short>() == ColumnType::UnsignedShort);
static_assert(logicalType<int>() == ColumnType::Int);
static_assert(logicalType<unsigned int>() == ColumnType::UnsignedInt);
static_assert(logicalType<long>() == (sizeof(long) > sizeof(int) ? ColumnType::LongLong : ColumnType::Int));
static_assert(logicalType<unsigned long>() ==
              (sizeof(unsigned long) > sizeof(unsigned int) ? ColumnType::UnsignedLongLong : ColumnType::UnsignedInt));
static_assert(logicalType<long long>() == ColumnType::LongLong);
static_assert(logicalType<unsigned long long>() == ColumnType::UnsignedLongLong);
static_assert(logicalType<float>() == ColumnType::Float);
static_assert(logicalType<double>() == ColumnType::Double);
static_assert(logicalType<std::string>() == ColumnType::String);
static_assert(logicalType<std::optional<int>>() == ColumnType::Int);
static_assert(logicalType<std::optional<std::string>>() == ColumnType::String);
static_assert(not isNullable<int>);
static_assert(isNullable<std::optional<int>>);

TEST(ColumnTypeTest, compileTimeLogicalTypesCoverEverySupportedStorageCategory)
{
    EXPECT_EQ(logicalType<bool>(), ColumnType::Bool);
    EXPECT_EQ(logicalType<unsigned int>(), ColumnType::UnsignedInt);
    EXPECT_EQ(logicalType<long long>(), ColumnType::LongLong);
    EXPECT_EQ(logicalType<double>(), ColumnType::Double);
    EXPECT_EQ(logicalType<std::optional<std::string>>(), ColumnType::String);
}

TEST(ColumnTypeTest, toStringCoversEveryColumnType)
{
    EXPECT_EQ(toString(ColumnType::Bool), "bool");
    EXPECT_EQ(toString(ColumnType::Char), "char");
    EXPECT_EQ(toString(ColumnType::UnsignedChar), "unsigned char");
    EXPECT_EQ(toString(ColumnType::Short), "short");
    EXPECT_EQ(toString(ColumnType::UnsignedShort), "unsigned short");
    EXPECT_EQ(toString(ColumnType::Int), "int");
    EXPECT_EQ(toString(ColumnType::UnsignedInt), "unsigned int");
    EXPECT_EQ(toString(ColumnType::LongLong), "long long");
    EXPECT_EQ(toString(ColumnType::UnsignedLongLong), "unsigned long long");
    EXPECT_EQ(toString(ColumnType::Float), "float");
    EXPECT_EQ(toString(ColumnType::Double), "double");
    EXPECT_EQ(toString(ColumnType::String), "std::string");
    EXPECT_EQ(toString(ColumnType::Uuid), "uuid");
}

TEST(ColumnTypeTest, toStringLabelsInvalidEnumValues)
{
    const auto invalidType = static_cast<ColumnType>(999); // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)

    EXPECT_EQ(toString(invalidType), "unknown");
}
