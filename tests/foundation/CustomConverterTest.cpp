module;

#include "tests/UnitTestPrelude.hpp"

module orm;

import :foundation;
import :model;
import :model_mapping;
import :model_columns;
import :query_value;

namespace converter_test
{
struct DomainUuid
{
    orm::Uuid value;
    bool reject{};
    auto operator==(const DomainUuid&) const -> bool = default;
};

struct BoxedInt
{
    int value{};
    auto operator==(const BoxedInt&) const -> bool = default;
};

struct OptionalText
{
    bool present{};
    std::string value;
    auto operator==(const OptionalText&) const -> bool = default;
};

struct ConvertedModel
{
    DomainUuid id;
    OptionalText note;
    BoxedInt count;
};
} // namespace converter_test

template <>
struct orm::ValueConverter<converter_test::DomainUuid>
{
    using Value = orm::Uuid;

    static auto toValue(const converter_test::DomainUuid& value) -> orm::Uuid
    {
        if (value.reject)
            throw std::invalid_argument{"converter detail that must stay private"};
        return value.value;
    }

    static auto fromValue(const orm::Uuid& value) -> converter_test::DomainUuid
    {
        return {.value = value, .reject = false};
    }
};

template <>
struct orm::ValueConverter<converter_test::BoxedInt>
{
    using Value = int;
    static auto toValue(const converter_test::BoxedInt& value) -> int
    {
        return value.value;
    }
    static auto fromValue(const int& value) -> converter_test::BoxedInt
    {
        return {.value = value};
    }
};

template <>
struct orm::NullableAdapter<converter_test::OptionalText>
{
    using Value = std::string;

    static auto hasValue(const converter_test::OptionalText& value) noexcept -> bool
    {
        return value.present;
    }
    static auto value(const converter_test::OptionalText& source) -> const std::string&
    {
        return source.value;
    }
    static auto make(std::string value) -> converter_test::OptionalText
    {
        return {.present = true, .value = std::move(value)};
    }
    static auto none() -> converter_test::OptionalText
    {
        return {};
    }
};

namespace
{
using converter_test::BoxedInt;
using converter_test::ConvertedModel;
using converter_test::DomainUuid;
using converter_test::OptionalText;

static_assert(orm::ValueConvertible<DomainUuid>);
static_assert(orm::ValueConvertible<BoxedInt>);
static_assert(!orm::ValueConvertible<int>);
static_assert(!orm::ValueConvertible<std::string>);
static_assert(orm::model::logicalType<DomainUuid>() == orm::model::ColumnType::Uuid);
static_assert(orm::model::logicalType<BoxedInt>() == orm::model::ColumnType::Int);
static_assert(orm::NullableAdaptable<OptionalText>);
static_assert(orm::model::isNullable<OptionalText>);
static_assert(!orm::model::isNullable<DomainUuid>);
static_assert(std::same_as<orm::model::detail::static_optional_value_t<OptionalText>, std::string>);

using Schema = orm::Schema<ConvertedModel>;
static_assert(orm::model::modelView<Schema, ConvertedModel>()->columns.size() == 3);
static_assert(orm::model::modelView<Schema, ConvertedModel>()->columns[0].type == orm::model::ColumnType::Uuid);
static_assert(orm::model::modelView<Schema, ConvertedModel>()->columns[1].type == orm::model::ColumnType::String);
static_assert(orm::model::modelView<Schema, ConvertedModel>()->columns[2].type == orm::model::ColumnType::Int);
static_assert(orm::model::modelView<Schema, ConvertedModel>()->columns[0].isNotNull == true);
static_assert(orm::model::modelView<Schema, ConvertedModel>()->columns[1].isNotNull == false);

TEST(CustomConverterTest, queryValuesUseTheDeclaredCanonicalValue)
{
    const auto uuid = orm::Uuid::parse("00112233-4455-6677-8899-aabbccddeeff");
    const orm::query::QueryValue convertedUuid{DomainUuid{uuid, false}};
    EXPECT_EQ(convertedUuid.getLogicalType(), orm::model::ColumnType::Uuid);
    EXPECT_EQ(std::get<orm::Uuid>(convertedUuid.get()), uuid);

    const orm::query::QueryValue convertedInt{BoxedInt{7}};
    EXPECT_EQ(convertedInt.getLogicalType(), orm::model::ColumnType::Int);
    EXPECT_EQ(std::get<int>(convertedInt.get()), 7);
}

TEST(CustomConverterTest, converterRejectionStaysAnApplicationException)
{
    const DomainUuid rejected{orm::Uuid{}, true};
    try
    {
        const orm::query::QueryValue value{rejected};
        FAIL() << "Expected the converter to reject the value";
    }
    catch (const std::invalid_argument& error)
    {
        EXPECT_EQ(std::string{error.what()}, std::string{"converter detail that must stay private"});
    }
}

TEST(CustomConverterTest, nullableAdapterRoundTripsPresence)
{
    const auto present = orm::NullableAdapter<OptionalText>::make(std::string{"hello"});
    EXPECT_TRUE(orm::NullableAdapter<OptionalText>::hasValue(present));
    EXPECT_EQ(orm::NullableAdapter<OptionalText>::value(present), std::string{"hello"});

    const auto absent = orm::NullableAdapter<OptionalText>::none();
    EXPECT_FALSE(orm::NullableAdapter<OptionalText>::hasValue(absent));
}
} // namespace
