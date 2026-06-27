#include "orm-cxx/query/QueryValue.hpp"

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <variant>

using orm::query::QueryValue;

namespace
{
template <typename Expected>
auto expectValue(const QueryValue& value, Expected expected) -> void
{
    EXPECT_EQ(std::get<Expected>(value.get()), expected);
}
} // namespace

TEST(QueryValueTest, shouldStoreSmallIntegralTypesAsInt)
{
    expectValue(QueryValue{true}, 1);
    expectValue(QueryValue{static_cast<char>(2)}, 2);
    expectValue(QueryValue{static_cast<signed char>(3)}, 3);
    expectValue(QueryValue{static_cast<unsigned char>(4)}, 4);
    expectValue(QueryValue{static_cast<short>(5)}, 5);
    expectValue(QueryValue{static_cast<unsigned short>(6)}, 6);
}

TEST(QueryValueTest, shouldStoreSignedWideIntegralTypesAsLongLong)
{
    expectValue(QueryValue{static_cast<long>(7)}, static_cast<long long>(7));
    expectValue(QueryValue{static_cast<long long>(8)}, static_cast<long long>(8));
}

TEST(QueryValueTest, shouldStoreUnsignedWideIntegralTypesAsUnsignedLongLong)
{
    expectValue(QueryValue{static_cast<unsigned int>(9)}, static_cast<unsigned long long>(9));
    expectValue(QueryValue{static_cast<unsigned long>(10)}, static_cast<unsigned long long>(10));
    expectValue(QueryValue{static_cast<unsigned long long>(11)}, static_cast<unsigned long long>(11));
}

TEST(QueryValueTest, shouldStoreFloatingPointTypesAsDouble)
{
    expectValue(QueryValue{static_cast<float>(1.5F)}, static_cast<double>(1.5F));
    expectValue(QueryValue{2.5}, 2.5);
}

TEST(QueryValueTest, shouldStoreStringViewAsString)
{
    const auto input = std::string_view{"view value"};

    expectValue(QueryValue{input}, std::string{"view value"});
}
