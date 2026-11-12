module;

#include <array>
#include <boost/optional.hpp>
#include <boost/uuid/uuid.hpp>
#include <cstddef>
#include <cstring>

#include "tests/UnitTestPrelude.hpp"

module orm;

import :internal;
import :test_support;

namespace boost_converter_models
{
struct Record
{
    int id;
    boost::uuids::uuid value;
    boost::optional<boost::uuids::uuid> optionalValue;
    inline static constexpr orm::reflection::FixedString table_name{"boost_converted_uuid_records"};
    inline static constexpr auto uuid_storage = orm::uuidStorages(
        orm::uuidStorage<&Record::value, orm::db::BackendType::Sqlite, orm::model::UuidStorage::Text36>(),
        orm::uuidStorage<&Record::optionalValue, orm::db::BackendType::Sqlite, orm::model::UuidStorage::Text36>());
};

using Schema = orm::Schema<Record>;
} // namespace boost_converter_models

template <>
struct orm::ValueConverter<boost::uuids::uuid>
{
    using Value = orm::Uuid;

    static auto toValue(const boost::uuids::uuid& value) -> orm::Uuid
    {
        std::array<std::byte, 16> bytes{};
        std::memcpy(bytes.data(), value.data, bytes.size());
        return orm::Uuid{bytes};
    }

    static auto fromValue(const orm::Uuid& value) -> boost::uuids::uuid
    {
        boost::uuids::uuid result{};
        std::memcpy(result.data, value.bytes().data(), value.bytes().size());
        return result;
    }
};

template <>
struct orm::NullableAdapter<boost::optional<boost::uuids::uuid>>
{
    using Value = boost::uuids::uuid;

    static auto hasValue(const boost::optional<boost::uuids::uuid>& value) noexcept -> bool
    {
        return bool{value};
    }
    static auto value(const boost::optional<boost::uuids::uuid>& source) -> const boost::uuids::uuid&
    {
        return *source;
    }
    static auto make(boost::uuids::uuid value) -> boost::optional<boost::uuids::uuid>
    {
        return boost::optional<boost::uuids::uuid>{value};
    }
    static auto none() -> boost::optional<boost::uuids::uuid>
    {
        return boost::none;
    }
};

namespace
{
using namespace orm::query;
namespace boosted = boost_converter_models;

static_assert(orm::ValueConvertible<boost::uuids::uuid>);
static_assert(orm::NullableAdaptable<boost::optional<boost::uuids::uuid>>);
static_assert(orm::model::isNullable<boost::optional<boost::uuids::uuid>>);
static_assert(orm::model::logicalType<boost::uuids::uuid>() == orm::model::ColumnType::Uuid);

auto uuid(unsigned char seed) -> boost::uuids::uuid
{
    std::array<std::byte, 16> bytes{};
    for (std::size_t index = 0; index < bytes.size(); ++index)
        bytes[index] = static_cast<std::byte>(seed + static_cast<unsigned char>(index));
    return orm::ValueConverter<boost::uuids::uuid>::fromValue(orm::Uuid{bytes});
}

TEST(BoostConverterTest, BoostUuidAndOptionalRoundTripThroughText36)
{
    orm::Database connection{orm::SqliteConfig{.database = orm::SqliteMemory{}}};
    auto context = connection.orm<boosted::Schema>();
    context.createTable<boosted::Record>();
    const auto first = uuid(1);
    const auto second = uuid(2);
    context.insert(boosted::Record{1, first, boost::optional<boost::uuids::uuid>{first}});
    context.insert(boosted::Record{2, second, boost::none});

    orm::Query<boosted::Record> all;
    all.orderBy(asc(col<&boosted::Record::id>()));
    const auto rows = context.select(all);
    ASSERT_EQ(rows.size(), 2U);
    EXPECT_EQ(rows[0].value, first);
    ASSERT_TRUE(rows[0].optionalValue);
    EXPECT_EQ(*rows[0].optionalValue, first);
    EXPECT_EQ(rows[1].value, second);
    EXPECT_FALSE(rows[1].optionalValue);

    orm::Query<boosted::Record> filtered;
    filtered.where(col<&boosted::Record::value>() == second);
    EXPECT_EQ(context.select(filtered).size(), 1U);

    orm::Query<boosted::Record> rawQuery;
    rawQuery.where(raw<boosted::Record>("value = :value", param("value", first, orm::model::UuidStorage::Text36)));
    EXPECT_EQ(context.select(rawQuery).size(), 1U);
}
} // namespace
