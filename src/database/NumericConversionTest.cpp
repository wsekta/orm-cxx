#include "orm-cxx/database/binding/NumericConversion.hpp"

#include <cmath>
#include <gtest/gtest.h>
#include <limits>

#include "orm-cxx/database/binding/NumericValue.hpp"

using orm::db::binding::checkedNumericCast;
using orm::db::binding::ConversionError;
using orm::db::binding::parseNumericValue;

TEST(NumericConversionTest, acceptsLosslessIntegralConversions)
{
    EXPECT_EQ(checkedNumericCast<short>(42, "value"), 42);
    EXPECT_EQ(checkedNumericCast<int>(true, "value"), 1);
    EXPECT_FALSE(checkedNumericCast<bool>(0, "value"));
    EXPECT_TRUE(checkedNumericCast<bool>(1, "value"));
}

TEST(NumericConversionTest, rejectsOutOfRangeIntegralConversions)
{
    EXPECT_THROW((void)checkedNumericCast<unsigned int>(-1, "value"), ConversionError);
    EXPECT_THROW((void)checkedNumericCast<short>(std::numeric_limits<int>::max(), "value"), ConversionError);
    EXPECT_THROW((void)checkedNumericCast<long long>(std::numeric_limits<unsigned long long>::max(), "value"),
                 ConversionError);
    EXPECT_THROW((void)checkedNumericCast<bool>(2, "value"), ConversionError);
}

TEST(NumericConversionTest, acceptsOnlyIntegralAndInRangeFloatingValuesForIntegralResults)
{
    EXPECT_EQ(checkedNumericCast<int>(42.0, "value"), 42);
    EXPECT_TRUE(checkedNumericCast<bool>(1.0, "value"));

    EXPECT_THROW((void)checkedNumericCast<int>(42.5, "value"), ConversionError);
    EXPECT_THROW((void)checkedNumericCast<unsigned int>(-1.0, "value"), ConversionError);
    EXPECT_THROW((void)checkedNumericCast<bool>(2.0, "value"), ConversionError);
    EXPECT_THROW((void)checkedNumericCast<long long>(std::ldexp(1.0, 63), "value"), ConversionError);
}

TEST(NumericConversionTest, rejectsNonFiniteSources)
{
    EXPECT_THROW((void)checkedNumericCast<double>(std::numeric_limits<double>::infinity(), "value"), ConversionError);
    EXPECT_THROW((void)checkedNumericCast<int>(std::numeric_limits<double>::quiet_NaN(), "value"), ConversionError);
}

TEST(NumericConversionTest, checksWhetherIntegralValuesAreExactlyRepresentableAsFloatingPoint)
{
    EXPECT_DOUBLE_EQ(checkedNumericCast<double>(42, "value"), 42.0);
    EXPECT_DOUBLE_EQ(checkedNumericCast<double>(1LL << 54, "value"), static_cast<double>(1LL << 54));
    EXPECT_THROW((void)checkedNumericCast<double>(std::numeric_limits<long long>::max(), "value"), ConversionError);
}

TEST(NumericConversionTest, checksNarrowingBetweenFloatingPointTypes)
{
    EXPECT_FLOAT_EQ(checkedNumericCast<float>(2.5, "value"), 2.5F);
    EXPECT_DOUBLE_EQ(checkedNumericCast<double>(0.1F, "value"), static_cast<double>(0.1F));

    EXPECT_THROW((void)checkedNumericCast<float>(0.1, "value"), ConversionError);
    EXPECT_THROW((void)checkedNumericCast<float>(std::numeric_limits<double>::max(), "value"), ConversionError);
}

TEST(NumericConversionTest, parsesDatabaseDecimalTextUsingLocaleIndependentSyntax)
{
    EXPECT_DOUBLE_EQ(parseNumericValue<double>("1234.5", "value"), 1234.5);
    EXPECT_THROW((void)parseNumericValue<double>("1234,5", "value"), ConversionError);
    EXPECT_THROW((void)parseNumericValue<double>("1234.5x", "value"), ConversionError);
}
