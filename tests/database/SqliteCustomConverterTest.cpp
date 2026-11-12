module;

#include "tests/UnitTestPrelude.hpp"

module orm;

import :internal;
import :migrations_internal;
import :test_support;

namespace sqlite_converter_models
{
struct DomainUuid
{
    orm::Uuid value;
    bool reject{};
    auto operator==(const DomainUuid&) const -> bool = default;
};

struct OptionalDomainUuid
{
    bool present{};
    DomainUuid value;
    auto operator==(const OptionalDomainUuid&) const -> bool = default;
};

struct Counter
{
    int value{};
    auto operator==(const Counter&) const -> bool = default;
};

struct Record
{
    int id;
    DomainUuid value;
    OptionalDomainUuid optionalValue;
    Counter counter;
    inline static constexpr orm::reflection::FixedString table_name{"converted_uuid_records"};
    inline static constexpr auto uuid_storage = orm::uuidStorages(
        orm::uuidStorage<&Record::value, orm::db::BackendType::Sqlite, orm::model::UuidStorage::Text36>(),
        orm::uuidStorage<&Record::optionalValue, orm::db::BackendType::Sqlite, orm::model::UuidStorage::Text36>());
};

struct RecordDto
{
    int id;
    DomainUuid value;
    OptionalDomainUuid optionalValue;
    Counter counter;
};

struct NativeUuidRecord
{
    int id;
    orm::Uuid value;
};

using Schema = orm::Schema<Record>;
} // namespace sqlite_converter_models

template <>
struct orm::ValueConverter<sqlite_converter_models::DomainUuid>
{
    using Value = orm::Uuid;

    static auto toValue(const sqlite_converter_models::DomainUuid& value) -> orm::Uuid
    {
        if (value.reject)
            throw std::invalid_argument{"sensitive converter detail"};
        return value.value;
    }

    static auto fromValue(const orm::Uuid& value) -> sqlite_converter_models::DomainUuid
    {
        return {.value = value, .reject = false};
    }
};

template <>
struct orm::ValueConverter<sqlite_converter_models::Counter>
{
    using Value = int;
    static auto toValue(const sqlite_converter_models::Counter& value) -> int
    {
        return value.value;
    }
    static auto fromValue(const int& value) -> sqlite_converter_models::Counter
    {
        return {.value = value};
    }
};

template <>
struct orm::NullableAdapter<sqlite_converter_models::OptionalDomainUuid>
{
    using Value = sqlite_converter_models::DomainUuid;

    static auto hasValue(const sqlite_converter_models::OptionalDomainUuid& value) noexcept -> bool
    {
        return value.present;
    }

    static auto value(const sqlite_converter_models::OptionalDomainUuid& source)
        -> const sqlite_converter_models::DomainUuid&
    {
        return source.value;
    }

    static auto make(sqlite_converter_models::DomainUuid value) -> sqlite_converter_models::OptionalDomainUuid
    {
        return {.present = true, .value = value};
    }

    static auto none() -> sqlite_converter_models::OptionalDomainUuid
    {
        return {};
    }
};

namespace
{
using namespace orm::query;
namespace converted = sqlite_converter_models;

static_assert(orm::ValueConvertible<converted::DomainUuid>);
static_assert(orm::ValueConvertible<converted::Counter>);
static_assert(orm::NullableAdaptable<converted::OptionalDomainUuid>);
static_assert(orm::model::isNullable<converted::OptionalDomainUuid>);
static_assert(orm::model::logicalType<converted::DomainUuid>() == orm::model::ColumnType::Uuid);
static_assert(orm::model::logicalType<converted::Counter>() == orm::model::ColumnType::Int);

auto pattern(unsigned int group) -> converted::DomainUuid
{
    std::array<std::byte, 16> bytes{};
    for (unsigned int index = 0; index < bytes.size(); ++index)
        bytes[index] = static_cast<std::byte>(group * 16 + index);
    return {.value = orm::Uuid{bytes}, .reject = false};
}

auto optionalPattern(unsigned int group) -> converted::OptionalDomainUuid
{
    return {.present = true, .value = pattern(group)};
}

template <typename Function>
auto expectConversion(Function&& operation) -> void
{
    try
    {
        operation();
        FAIL() << "Expected a conversion error";
    }
    catch (const orm::DatabaseError& error)
    {
        EXPECT_EQ(error.getCode(), orm::DatabaseErrorCode::Conversion);
        EXPECT_EQ(std::string{error.what()}.find("sensitive converter detail"), std::string::npos);
    }
}

TEST(SqliteCustomConverterTest, Text36AndConvertedValuesRoundTripThroughCrudProjectionAndStaticPlans)
{
    orm::Database connection{orm::SqliteConfig{.database = orm::SqliteMemory{}}};
    auto context = connection.orm<converted::Schema>();
    context.createTable<converted::Record>();
    context.insert(converted::Record{1, pattern(1), optionalPattern(1), {11}});
    context.insert(converted::Record{2, pattern(2), {}, {22}});

    auto& session = orm::migrations::detail::DatabaseAccess::session(connection);
    int malformedText{}, nulls{};
    session << "SELECT COUNT(*) FROM converted_uuid_records WHERE typeof(value) <> 'text' OR length(value) <> 36",
        soci::into(malformedText);
    session << "SELECT COUNT(*) FROM converted_uuid_records WHERE optionalValue IS NULL", soci::into(nulls);
    EXPECT_EQ(malformedText, 0);
    EXPECT_EQ(nulls, 1);

    std::string tableSql;
    session << "SELECT sql FROM sqlite_master WHERE name = 'converted_uuid_records'", soci::into(tableSql);
    EXPECT_NE(tableSql.find("value TEXT NOT NULL"), std::string::npos);
    EXPECT_NE(tableSql.find("optionalValue TEXT"), std::string::npos);

    orm::Query<converted::Record> all;
    all.orderBy(asc(col<&converted::Record::id>()));
    const auto rows = context.select(all);
    ASSERT_EQ(rows.size(), 2U);
    EXPECT_EQ(rows[0].value, pattern(1));
    EXPECT_EQ(rows[0].optionalValue, optionalPattern(1));
    EXPECT_EQ(rows[0].counter, converted::Counter{11});
    EXPECT_EQ(rows[1].value, pattern(2));
    EXPECT_FALSE(rows[1].optionalValue.present);
    EXPECT_EQ(rows[1].counter, converted::Counter{22});

    orm::Query<converted::Record> filtered;
    filtered.where(col<&converted::Record::value>() == pattern(2));
    ASSERT_EQ(context.select(filtered).size(), 1U);

    constexpr auto lookup =
        select<converted::Record>().where(col<&converted::Record::value>() == param<converted::DomainUuid, 0>());
    ASSERT_EQ(context.select(lookup, pattern(2)).size(), 1U);

    orm::ProjectionQuery<converted::Record, converted::RecordDto> dto;
    dto.project(as("id", col<&converted::Record::id>()), as("value", col<&converted::Record::value>()),
                as("optionalValue", col<&converted::Record::optionalValue>()),
                as("counter", col<&converted::Record::counter>()))
        .where(col<&converted::Record::id>() == 0);
    const auto projection = context.select(dto);
    ASSERT_EQ(projection.size(), 1U);
    EXPECT_EQ(projection.front().value, pattern(1));
    EXPECT_EQ(projection.front().optionalValue, optionalPattern(1));
    EXPECT_EQ(projection.front().counter, converted::Counter{11});

    constexpr auto staticDto =
        selectAs<converted::Record, converted::RecordDto>(as<"id">(col<&converted::Record::id>()),
                                                          as<"value">(col<&converted::Record::value>()),
                                                          as<"optionalValue">(col<&converted::Record::optionalValue>()),
                                                          as<"counter">(col<&converted::Record::counter>()))
            .where(col<&converted::Record::id>() == param<int, 0>());
    const auto projected = context.select(staticDto, 2);
    ASSERT_EQ(projected.size(), 1U);
    EXPECT_EQ(projected.front().value, pattern(2));
    EXPECT_FALSE(projected.front().optionalValue.present);
    EXPECT_EQ(projected.front().counter, converted::Counter{22});

    orm::Update<converted::Record> update;
    update.set(col<&converted::Record::value>(), pattern(7))
        .set(col<&converted::Record::counter>(), converted::Counter{77})
        .where(col<&converted::Record::id>() == 0);
    EXPECT_EQ(context.update(update), 1U);
    orm::Query<converted::Record> changed;
    changed.where(col<&converted::Record::id>() == 0);
    const auto changedRows = context.select(changed);
    ASSERT_EQ(changedRows.size(), 1U);
    EXPECT_EQ(changedRows.front().value, pattern(7));
    EXPECT_EQ(changedRows.front().counter, converted::Counter{77});

    EXPECT_EQ(context.remove<converted::Record>(col<&converted::Record::id>() == 1), 1U);
}

TEST(SqliteCustomConverterTest, RawSqlUsesTheExplicitText36Representation)
{
    orm::Database connection{orm::SqliteConfig{.database = orm::SqliteMemory{}}};
    auto context = connection.orm<converted::Schema>();
    context.createTable<converted::Record>();
    const auto value = pattern(3);
    context.insert(converted::Record{1, value, {}, {}});

    orm::Query<converted::Record> query;
    query.where(raw<converted::Record>("value = :value", param("value", value, orm::model::UuidStorage::Text36)));
    EXPECT_EQ(context.select(query).size(), 1U);

    orm::Query<converted::Record> missing;
    missing.where(raw<converted::Record>("value = :value", param("value", value)));
    EXPECT_THROW((void)context.select(missing), std::invalid_argument);

    orm::Query<converted::Record> null;
    null.where(raw<converted::Record>(
        "id = :id", param("id", 1),
        param("unused", std::nullopt, orm::model::ColumnType::Uuid, orm::model::UuidStorage::Text36)));
    EXPECT_EQ(context.select(null).size(), 1U);
}

TEST(SqliteCustomConverterTest, CapturesCustomNullableValuesAsOwnedParameters)
{
    constexpr auto native = orm::query::detail::captureValue(std::optional<int>{7});
    static_assert(native.has_value() && *native == 7);

    auto source = optionalPattern(3);
    const auto captured = orm::query::detail::captureValue(source);
    source.value = pattern(4);
    ASSERT_TRUE(captured.has_value());
    EXPECT_EQ(*captured, pattern(3));

    const auto absent = orm::query::detail::captureValue(converted::OptionalDomainUuid{});
    EXPECT_FALSE(absent.has_value());

    orm::Database connection{orm::SqliteConfig{.database = orm::SqliteMemory{}}};
    auto context = connection.orm<converted::Schema>();
    context.createTable<converted::Record>();
    context.insert(converted::Record{1, pattern(3), {}, {}});
    orm::Query<converted::Record> query;
    query.where(raw<converted::Record>("value = :value", param("value", *captured, orm::model::UuidStorage::Text36)));
    EXPECT_EQ(context.select(query).size(), 1U);
}

TEST(SqliteCustomConverterTest, RawSqlOptionalConvertedUuidBindsValueAndNull)
{
    orm::Database connection{orm::SqliteConfig{.database = orm::SqliteMemory{}}};
    auto context = connection.orm<converted::Schema>();
    context.createTable<converted::Record>();
    context.insert(converted::Record{1, pattern(3), optionalPattern(3), {}});
    context.insert(converted::Record{2, pattern(4), {}, {}});

    const std::optional<converted::DomainUuid> present{pattern(3)};
    orm::Query<converted::Record> query;
    query.where(
        raw<converted::Record>("optionalValue = :value", param("value", present, orm::model::UuidStorage::Text36)));
    const auto rows = context.select(query);
    ASSERT_EQ(rows.size(), 1U);
    EXPECT_EQ(rows.front().value, pattern(3));

    const std::optional<converted::DomainUuid> absent;
    orm::Query<converted::Record> nullQuery;
    nullQuery.where(
        raw<converted::Record>("optionalValue IS :value", param("value", absent, orm::model::UuidStorage::Text36)));
    const auto nullRows = context.select(nullQuery);
    ASSERT_EQ(nullRows.size(), 1U);
    EXPECT_EQ(nullRows.front().value, pattern(4));
}

TEST(SqliteCustomConverterTest, InvalidTextStorageClassAndTextFailDuringRead)
{
    for (const auto* invalid : {"'not-a-uuid'", "42"})
    {
        SCOPED_TRACE(invalid);
        orm::Database connection{orm::SqliteConfig{.database = orm::SqliteMemory{}}};
        auto context = connection.orm<converted::Schema>();
        context.createTable<converted::Record>();
        auto& session = orm::migrations::detail::DatabaseAccess::session(connection);
        session << std::string{"INSERT INTO converted_uuid_records(id,value,optionalValue,counter) VALUES(1,"} +
                       invalid + ",NULL,0)";
        orm::Query<converted::Record> all;
        expectConversion([&] { (void)context.select(all); });
    }
}

TEST(SqliteCustomConverterTest, ConverterRefusalIsSanitizedAndDoesNotInsert)
{
    orm::Database connection{orm::SqliteConfig{.database = orm::SqliteMemory{}}};
    auto context = connection.orm<converted::Schema>();
    context.createTable<converted::Record>();
    auto rejected = pattern(1);
    rejected.reject = true;
    expectConversion([&] { context.insert(converted::Record{1, rejected, {}, {}}); });
    auto& session = orm::migrations::detail::DatabaseAccess::session(connection);
    int count{};
    session << "SELECT COUNT(*) FROM converted_uuid_records", soci::into(count);
    EXPECT_EQ(count, 0);
}

TEST(SqliteCustomConverterTest, NativeUuidWithoutExplicitRepresentationStaysUnsupported)
{
    orm::Database connection{orm::SqliteConfig{.database = orm::SqliteMemory{}}};
    auto context = connection.orm<orm::Schema<converted::NativeUuidRecord>>();
    try
    {
        context.insert(converted::NativeUuidRecord{1, orm::Uuid{}});
        FAIL() << "Expected the SQLite backend to reject a native UUID";
    }
    catch (const orm::DatabaseError& error)
    {
        EXPECT_EQ(error.getCode(), orm::DatabaseErrorCode::UnsupportedFeature);
    }
}
} // namespace
