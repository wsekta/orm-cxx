#include "orm-cxx/query/QueryValue.hpp"

#include <cstdint>
#include <gtest/gtest.h>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

using orm::query::QueryValue;

namespace
{
template <typename Expected>
auto expectValue(const QueryValue& value, Expected expected, orm::model::ColumnType expectedLogicalType) -> void
{
    EXPECT_EQ(std::get<Expected>(value.get()), expected);
    EXPECT_EQ(value.getLogicalType(), expectedLogicalType);
}
} // namespace

TEST(QueryValueTest, shouldStoreSmallIntegralTypesAsInt)
{
    expectValue(QueryValue{true}, 1, orm::model::ColumnType::Bool);
    expectValue(QueryValue{static_cast<char>(2)}, 2, orm::model::ColumnType::Char);
    expectValue(QueryValue{static_cast<signed char>(3)}, 3, orm::model::ColumnType::Char);
    expectValue(QueryValue{static_cast<unsigned char>(4)}, 4, orm::model::ColumnType::UnsignedChar);
    expectValue(QueryValue{static_cast<short>(5)}, 5, orm::model::ColumnType::Short);
    expectValue(QueryValue{static_cast<unsigned short>(6)}, 6, orm::model::ColumnType::UnsignedShort);
    expectValue(QueryValue{7}, 7, orm::model::ColumnType::Int);
}

TEST(QueryValueTest, shouldStoreLongWithoutNarrowingOnLlp64AndLp64)
{
    const auto input = std::numeric_limits<long>::max();

    if constexpr (sizeof(long) > sizeof(int))
    {
        expectValue(QueryValue{input}, static_cast<long long>(input), orm::model::ColumnType::LongLong);
    }
    else
    {
        expectValue(QueryValue{input}, static_cast<int>(input), orm::model::ColumnType::Int);
    }

    expectValue(QueryValue{static_cast<long long>(8)}, static_cast<long long>(8), orm::model::ColumnType::LongLong);
}

TEST(QueryValueTest, shouldStoreUnsignedWideIntegralTypesAsUnsignedLongLong)
{
    expectValue(QueryValue{static_cast<unsigned int>(9)}, static_cast<unsigned long long>(9),
                orm::model::ColumnType::UnsignedInt);
    const auto unsignedLongType = sizeof(unsigned long) > sizeof(unsigned int) ?
                                      orm::model::ColumnType::UnsignedLongLong :
                                      orm::model::ColumnType::UnsignedInt;
    expectValue(QueryValue{std::numeric_limits<unsigned long>::max()},
                static_cast<unsigned long long>(std::numeric_limits<unsigned long>::max()), unsignedLongType);
    expectValue(QueryValue{static_cast<unsigned long long>(11)}, static_cast<unsigned long long>(11),
                orm::model::ColumnType::UnsignedLongLong);
}

TEST(QueryValueTest, fixedWidth64BitTypesUse64BitLogicalAndStorageTypes)
{
    const auto signedValue = std::numeric_limits<std::int64_t>::max();
    const auto unsignedValue = std::numeric_limits<std::uint64_t>::max();

    expectValue(QueryValue{signedValue}, static_cast<long long>(signedValue), orm::model::ColumnType::LongLong);
    expectValue(QueryValue{unsignedValue}, static_cast<unsigned long long>(unsignedValue),
                orm::model::ColumnType::UnsignedLongLong);
}

TEST(QueryValueTest, shouldStoreFloatingPointTypesAsDouble)
{
    expectValue(QueryValue{static_cast<float>(1.5F)}, static_cast<double>(1.5F), orm::model::ColumnType::Float);
    expectValue(QueryValue{2.5}, 2.5, orm::model::ColumnType::Double);
}

TEST(QueryValueTest, shouldStoreStringInputsAsString)
{
    const auto input = std::string_view{"view value"};

    expectValue(QueryValue{input}, std::string{"view value"}, orm::model::ColumnType::String);
    expectValue(QueryValue{std::string{"owned value"}}, std::string{"owned value"}, orm::model::ColumnType::String);
    expectValue(QueryValue{"literal value"}, std::string{"literal value"}, orm::model::ColumnType::String);
}

TEST(QueryValueTest, shouldPreserveOptionalValueLogicalType)
{
    const auto value = std::optional<unsigned short>{static_cast<unsigned short>(42)};

    expectValue(QueryValue{value}, 42, orm::model::ColumnType::UnsignedShort);
}

TEST(QueryValueTest, shouldRestoreAndCompareCanonicalValuesWithLogicalType)
{
    const auto restored = QueryValue::fromStorage(orm::model::ColumnType::Short, QueryValue::Value{42});

    expectValue(restored, 42, orm::model::ColumnType::Short);
    EXPECT_EQ(restored, QueryValue{static_cast<short>(42)});
    EXPECT_NE(restored, QueryValue{42});
}

TEST(QueryValueTest, shouldRejectCanonicalStorageThatDoesNotMatchLogicalType)
{
    EXPECT_THROW((void)QueryValue::fromStorage(orm::model::ColumnType::String, QueryValue::Value{42}),
                 std::invalid_argument);
    EXPECT_THROW((void)QueryValue::fromStorage(orm::model::ColumnType::LongLong, QueryValue::Value{42}),
                 std::invalid_argument);
}

TEST(QueryValueTest, shouldRejectCanonicalStorageOutsideItsLogicalRange)
{
    EXPECT_THROW((void)QueryValue::fromStorage(orm::model::ColumnType::Bool, QueryValue::Value{2}),
                 std::invalid_argument);
    EXPECT_THROW((void)QueryValue::fromStorage(orm::model::ColumnType::UnsignedChar, QueryValue::Value{300}),
                 std::invalid_argument);
    EXPECT_THROW((void)QueryValue::fromStorage(orm::model::ColumnType::UnsignedInt,
                                               QueryValue::Value{std::numeric_limits<unsigned long long>::max()}),
                 std::invalid_argument);
    EXPECT_THROW((void)QueryValue::fromStorage(orm::model::ColumnType::Float, QueryValue::Value{0.1}),
                 std::invalid_argument);
    EXPECT_THROW((void)QueryValue::fromStorage(orm::model::ColumnType::Double,
                                               QueryValue::Value{std::numeric_limits<double>::infinity()}),
                 std::invalid_argument);
}

TEST(QueryValueTest, shouldAcceptCanonicalStorageAtLogicalBoundaries)
{
    EXPECT_NO_THROW((void)QueryValue::fromStorage(orm::model::ColumnType::UnsignedChar, QueryValue::Value{255}));
    EXPECT_NO_THROW((void)QueryValue::fromStorage(
        orm::model::ColumnType::UnsignedInt,
        QueryValue::Value{static_cast<unsigned long long>(std::numeric_limits<unsigned int>::max())}));
    EXPECT_NO_THROW(
        (void)QueryValue::fromStorage(orm::model::ColumnType::Float, QueryValue::Value{static_cast<double>(0.1F)}));
}

TEST(QueryValueTest, unsupportedAndInvalidEnumValuesShouldHaveNoCompatibleStorage)
{
    const auto storedValue = QueryValue::Value{0};

    EXPECT_FALSE(QueryValue::isCompatibleStorage(orm::model::ColumnType::Uuid, storedValue));
    EXPECT_FALSE(QueryValue::isCompatibleStorage(
        static_cast<orm::model::ColumnType>(999), // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
        storedValue));
}
