module;

#include "tests/UnitTestPrelude.hpp"

module orm;

import :internal;
import :migrations_internal;
import :test_support;

namespace sqlite_binary_uuid_models
{
struct UserId
{
    orm::Uuid value;
    bool reject{};
    auto operator==(const UserId&) const -> bool = default;
};

struct NonDefaultId
{
    explicit NonDefaultId(orm::Uuid input) : value{input} {}
    NonDefaultId() = delete;
    NonDefaultId(const NonDefaultId&) = default;
    NonDefaultId(NonDefaultId&&) = default;
    auto operator=(const NonDefaultId&) -> NonDefaultId& = delete;
    auto operator=(NonDefaultId&&) -> NonDefaultId& = delete;

    orm::Uuid value;
    inline static int adapterCalls = 0;
};

struct CopyOnlyId
{
    explicit CopyOnlyId(orm::Uuid input) : value{input} {}
    CopyOnlyId() = delete;
    CopyOnlyId(const CopyOnlyId&) = default;
    CopyOnlyId(CopyOnlyId&&) = delete;
    auto operator=(const CopyOnlyId&) -> CopyOnlyId& = delete;
    auto operator=(CopyOnlyId&&) -> CopyOnlyId& = delete;

    orm::Uuid value;
};
} // namespace sqlite_binary_uuid_models

template <>
struct orm::UuidAdapter<sqlite_binary_uuid_models::UserId>
{
    static auto toUuid(const sqlite_binary_uuid_models::UserId& value) -> orm::Uuid
    {
        if (value.reject)
            throw std::invalid_argument{"sensitive adapter detail"};
        return value.value;
    }
    static auto fromUuid(const orm::Uuid& value) -> sqlite_binary_uuid_models::UserId
    {
        return {value, false};
    }
};

template <>
struct orm::UuidAdapter<sqlite_binary_uuid_models::NonDefaultId>
{
    static auto toUuid(const sqlite_binary_uuid_models::NonDefaultId& value) -> orm::Uuid
    {
        return value.value;
    }
    static auto fromUuid(const orm::Uuid& value) -> sqlite_binary_uuid_models::NonDefaultId
    {
        ++sqlite_binary_uuid_models::NonDefaultId::adapterCalls;
        if (value.bytes()[0] == std::byte{0xff})
            throw std::invalid_argument{"sensitive adapter detail"};
        if (value.bytes()[0] == std::byte{0xfe})
            throw std::bad_alloc{};
        return sqlite_binary_uuid_models::NonDefaultId{value};
    }
};

template <>
struct orm::UuidAdapter<sqlite_binary_uuid_models::CopyOnlyId>
{
    static auto toUuid(const sqlite_binary_uuid_models::CopyOnlyId& value) -> orm::Uuid
    {
        return value.value;
    }
    static auto fromUuid(const orm::Uuid& value) -> sqlite_binary_uuid_models::CopyOnlyId
    {
        return sqlite_binary_uuid_models::CopyOnlyId{value};
    }
};

namespace sqlite_binary_uuid_models
{
struct Record
{
    int id;
    UserId value;
    std::optional<UserId> optionalValue;
    inline static constexpr orm::reflection::FixedString table_name{"binary_uuid_records"};
    inline static constexpr auto uuid_storage = orm::uuidStorages(
        orm::uuidStorage<&Record::value, orm::db::BackendType::Sqlite, orm::model::UuidStorage::Binary16>(),
        orm::uuidStorage<&Record::optionalValue, orm::db::BackendType::Sqlite, orm::model::UuidStorage::Binary16>());
};
struct RecordDto
{
    int id;
    UserId value;
    std::optional<UserId> optionalValue;
};
struct NonDefaultRecord
{
    int id;
    std::optional<NonDefaultId> value;
    inline static constexpr orm::reflection::FixedString table_name{"binary_uuid_nondefault"};
    inline static constexpr auto uuid_storage = orm::uuidStorages(
        orm::uuidStorage<&NonDefaultRecord::value, orm::db::BackendType::Sqlite, orm::model::UuidStorage::Binary16>());
};
struct NonDefaultDto
{
    std::optional<NonDefaultId> value;
};
using NonDefaultSchema = orm::Schema<NonDefaultRecord>;
struct CopyOnlyRecord
{
    int id;
    std::optional<CopyOnlyId> value;
    inline static constexpr orm::reflection::FixedString table_name{"binary_uuid_copyonly"};
    inline static constexpr auto uuid_storage = orm::uuidStorages(
        orm::uuidStorage<&CopyOnlyRecord::value, orm::db::BackendType::Sqlite, orm::model::UuidStorage::Binary16>());
};
struct CopyOnlyDto
{
    std::optional<CopyOnlyId> value;
};
using CopyOnlySchema = orm::Schema<CopyOnlyRecord>;
struct Tag
{
    UserId id;
    inline static constexpr orm::reflection::FixedString table_name{"binary_uuid_tags"};
    inline static constexpr auto uuid_storage = orm::uuidStorages(
        orm::uuidStorage<&Tag::id, orm::db::BackendType::Sqlite, orm::model::UuidStorage::Binary16>());
};
struct Owner
{
    UserId id;
    std::optional<Tag> target;
    orm::ManyToMany<Tag> tags;
    inline static constexpr orm::reflection::FixedString table_name{"binary_uuid_owners"};
    inline static constexpr auto uuid_storage = orm::uuidStorages(
        orm::uuidStorage<&Owner::id, orm::db::BackendType::Sqlite, orm::model::UuidStorage::Binary16>());
    inline static constexpr auto relations = orm::relations(orm::manyToMany<&Owner::tags>()
                                                                .through<"binary_uuid_links">()
                                                                .ownerColumns<"owner_id">()
                                                                .targetColumns<"tag_id">());
};
using Schema = orm::Schema<Record, Tag, Owner>;
} // namespace sqlite_binary_uuid_models

namespace
{
using namespace orm::query;
namespace uuid_models = sqlite_binary_uuid_models;

static_assert(orm::UuidAdaptable<uuid_models::NonDefaultId>);
static_assert(!std::is_default_constructible_v<uuid_models::NonDefaultId>);
static_assert(!std::is_move_assignable_v<uuid_models::NonDefaultId>);
static_assert(std::is_move_constructible_v<uuid_models::NonDefaultDto>);
static_assert(orm::UuidAdaptable<uuid_models::CopyOnlyId>);
static_assert(!std::is_default_constructible_v<uuid_models::CopyOnlyId>);
static_assert(std::is_copy_constructible_v<uuid_models::CopyOnlyId>);
static_assert(!std::is_move_constructible_v<uuid_models::CopyOnlyId>);
static_assert(std::is_move_constructible_v<uuid_models::CopyOnlyDto>);

auto pattern(unsigned int group) -> uuid_models::UserId
{
    std::array<std::byte, 16> bytes{};
    for (unsigned int index = 0; index < bytes.size(); ++index)
        bytes[index] = static_cast<std::byte>(group * 16 + index);
    return {orm::Uuid{bytes}, false};
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
        EXPECT_EQ(std::string{error.what()}.find("sensitive adapter detail"), std::string::npos);
    }
}

TEST(SqliteBinaryUuidTest, CrudDynamicStaticDtoAndEveryByteRoundTrip)
{
    orm::Database connection{orm::SqliteConfig{.database = orm::SqliteMemory{}}};
    auto context = connection.orm<uuid_models::Schema>();
    context.createTable<uuid_models::Record>();
    for (unsigned int group = 0; group < 16; ++group)
        context.insert(uuid_models::Record{static_cast<int>(group), pattern(group),
                                           group == 0 ? std::nullopt : std::optional{pattern(group)}});
    const uuid_models::UserId nil{orm::Uuid{}, false};
    context.insert(uuid_models::Record{16, nil, nil});

    auto& session = orm::migrations::detail::DatabaseAccess::session(connection);
    int malformed{}, nulls{};
    session << "SELECT COUNT(*) FROM binary_uuid_records WHERE typeof(value) <> 'blob' OR length(value) <> 16",
        soci::into(malformed);
    session << "SELECT COUNT(*) FROM binary_uuid_records WHERE optionalValue IS NULL", soci::into(nulls);
    EXPECT_EQ(malformed, 0);
    EXPECT_EQ(nulls, 1);

    orm::Query<uuid_models::Record> all;
    all.orderBy(asc(col<&uuid_models::Record::id>()));
    const auto rows = context.select(all);
    ASSERT_EQ(rows.size(), 17U);
    for (unsigned int group = 0; group < 16; ++group)
    {
        EXPECT_EQ(rows[group].value, pattern(group));
        if (group == 0)
            EXPECT_FALSE(rows[group].optionalValue);
        else
            EXPECT_EQ(rows[group].optionalValue, pattern(group));
    }
    EXPECT_EQ(rows.back().value, nil);
    EXPECT_EQ(rows.back().optionalValue, nil);

    orm::Query<uuid_models::Record> dynamic;
    dynamic.where(col<&uuid_models::Record::value>() == pattern(7));
    ASSERT_EQ(context.select(dynamic).size(), 1U);
    constexpr auto lookup =
        select<uuid_models::Record>().where(col<&uuid_models::Record::value>() == param<uuid_models::UserId, 0>());
    ASSERT_EQ(context.select(lookup, pattern(7)).size(), 1U);
    auto dynamicPlan = lookup.toDynamic(pattern(7));
    ASSERT_EQ(context.select(dynamicPlan).size(), 1U);

    orm::ProjectionQuery<uuid_models::Record, uuid_models::RecordDto> dto;
    dto.project(as("id", col<&uuid_models::Record::id>()), as("value", col<&uuid_models::Record::value>()),
                as("optionalValue", col<&uuid_models::Record::optionalValue>()))
        .where(col<&uuid_models::Record::id>() == 0);
    const auto projection = context.select(dto);
    ASSERT_EQ(projection.size(), 1U);
    EXPECT_EQ(projection.front().value, pattern(0));
    EXPECT_FALSE(projection.front().optionalValue);

    constexpr auto staticDto =
        selectAs<uuid_models::Record, uuid_models::RecordDto>(
            as<"id">(col<&uuid_models::Record::id>()), as<"value">(col<&uuid_models::Record::value>()),
            as<"optionalValue">(col<&uuid_models::Record::optionalValue>()))
            .where(col<&uuid_models::Record::id>() == param<int, 0>());
    const auto projected = context.select(staticDto, 7);
    ASSERT_EQ(projected.size(), 1U);
    EXPECT_EQ(projected.front().value, pattern(7));
    EXPECT_EQ(projected.front().optionalValue, pattern(7));

    orm::Update<uuid_models::Record> update;
    update.set(col<&uuid_models::Record::value>(), pattern(15)).where(col<&uuid_models::Record::id>() == 0);
    EXPECT_EQ(context.update(update), 1U);
    EXPECT_EQ(context.remove<uuid_models::Record>(col<&uuid_models::Record::id>() == 15), 1U);
    orm::Query<uuid_models::Record> changed;
    changed.where(col<&uuid_models::Record::id>() == 0);
    const auto changedRows = context.select(changed);
    ASSERT_EQ(changedRows.size(), 1U);
    EXPECT_EQ(changedRows.front().value, pattern(15));
}

TEST(SqliteBinaryUuidTest, RawSqlRequiresAndUsesExplicitBinaryContext)
{
    orm::Database connection{orm::SqliteConfig{.database = orm::SqliteMemory{}}};
    auto context = connection.orm<uuid_models::Schema>();
    context.createTable<uuid_models::Record>();
    const auto value = pattern(2);
    context.insert(uuid_models::Record{1, value, std::nullopt});
    orm::Query<uuid_models::Record> query;
    query.where(raw<uuid_models::Record>("value = :value", param("value", value, orm::model::UuidStorage::Binary16)));
    EXPECT_EQ(context.select(query).size(), 1U);
    orm::Query<uuid_models::Record> missing;
    missing.where(raw<uuid_models::Record>("value = :value", param("value", value)));
    EXPECT_THROW((void)context.select(missing), std::invalid_argument);

    // The backend identifies actual placeholders, including exact names, without
    // interpreting quoted SQL text as a bound parameter.
    orm::Query<uuid_models::Record> extra;
    extra.where(raw<uuid_models::Record>("id = :value_long AND ':value' = ':value'", param("value_long", 1),
                                         param("value", value, orm::model::UuidStorage::Binary16)));
    EXPECT_EQ(context.select(extra).size(), 1U);

    orm::Query<uuid_models::Record> extraNull;
    extraNull.where(raw<uuid_models::Record>(
        "id = :id", param("id", 1),
        param("unused", std::nullopt, orm::model::ColumnType::Uuid, orm::model::UuidStorage::Binary16)));
    EXPECT_EQ(context.select(extraNull).size(), 1U);
}

TEST(SqliteBinaryUuidTest, InvalidStorageClassAndLengthFailDuringRead)
{
    for (const auto* invalid : {"'0123456789abcdef'", "X'00'", "zeroblob(17)"})
    {
        SCOPED_TRACE(invalid);
        orm::Database connection{orm::SqliteConfig{.database = orm::SqliteMemory{}}};
        auto context = connection.orm<uuid_models::Schema>();
        context.createTable<uuid_models::Record>();
        auto& session = orm::migrations::detail::DatabaseAccess::session(connection);
        session << std::string{"INSERT INTO binary_uuid_records(id,value,optionalValue) VALUES(1,"} + invalid +
                       ",NULL)";
        orm::Query<uuid_models::Record> all;
        expectConversion([&] { (void)context.select(all); });
    }
}

TEST(SqliteBinaryUuidTest, AdapterRefusalIsSanitizedAndDoesNotInsert)
{
    orm::Database connection{orm::SqliteConfig{.database = orm::SqliteMemory{}}};
    auto context = connection.orm<uuid_models::Schema>();
    context.createTable<uuid_models::Record>();
    auto invalid = pattern(1);
    invalid.reject = true;
    expectConversion([&] { context.insert(uuid_models::Record{1, invalid, std::nullopt}); });
    int count{};
    auto& session = orm::migrations::detail::DatabaseAccess::session(connection);
    session << "SELECT COUNT(*) FROM binary_uuid_records", soci::into(count);
    EXPECT_EQ(count, 0);
}

TEST(SqliteBinaryUuidTest, OptionalDtoSupportsUuidTypeWithoutDefaultConstructionOrAssignment)
{
    orm::Database connection{orm::SqliteConfig{.database = orm::SqliteMemory{}}};
    auto context = connection.orm<uuid_models::NonDefaultSchema>();
    context.createTable<uuid_models::NonDefaultRecord>();
    auto& session = orm::migrations::detail::DatabaseAccess::session(connection);
    session << "INSERT INTO binary_uuid_nondefault(id,value) VALUES"
               "(1,NULL),(2,X'000102030405060708090a0b0c0d0e0f'),"
               "(3,X'ff000000000000000000000000000000'),(4,X'fe000000000000000000000000000000')";

    constexpr auto query = selectAs<uuid_models::NonDefaultRecord, uuid_models::NonDefaultDto>(
                               as<"value">(col<&uuid_models::NonDefaultRecord::value>()))
                               .where(col<&uuid_models::NonDefaultRecord::id>() == param<int, 0>());
    uuid_models::NonDefaultId::adapterCalls = 0;
    const auto nullRow = context.select(query, 1);
    ASSERT_EQ(nullRow.size(), 1U);
    EXPECT_FALSE(nullRow.front().value);
    EXPECT_EQ(uuid_models::NonDefaultId::adapterCalls, 0);

    const auto presentRow = context.select(query, 2);
    ASSERT_EQ(presentRow.size(), 1U);
    ASSERT_TRUE(presentRow.front().value);
    EXPECT_EQ(presentRow.front().value->value, pattern(0).value);
    EXPECT_EQ(uuid_models::NonDefaultId::adapterCalls, 1);

    expectConversion([&] { (void)context.select(query, 3); });
    EXPECT_THROW((void)context.select(query, 4), std::bad_alloc);
}

TEST(SqliteBinaryUuidTest, OptionalDtoSupportsCopyOnlyAdaptedUuid)
{
    orm::Database connection{orm::SqliteConfig{.database = orm::SqliteMemory{}}};
    auto context = connection.orm<uuid_models::CopyOnlySchema>();
    context.createTable<uuid_models::CopyOnlyRecord>();
    auto& session = orm::migrations::detail::DatabaseAccess::session(connection);
    session << "INSERT INTO binary_uuid_copyonly(id,value) VALUES(1,X'000102030405060708090a0b0c0d0e0f')";

    constexpr auto query = selectAs<uuid_models::CopyOnlyRecord, uuid_models::CopyOnlyDto>(
        as<"value">(col<&uuid_models::CopyOnlyRecord::value>()));
    const auto rows = context.select(query);
    ASSERT_EQ(rows.size(), 1U);
    ASSERT_TRUE(rows.front().value);
    EXPECT_EQ(rows.front().value->value, pattern(0).value);
}

TEST(SqliteBinaryUuidTest, BinaryKeysPropagateThroughToOneAndJunctionRelations)
{
    orm::Database connection{orm::SqliteConfig{.database = orm::SqliteMemory{}}};
    auto context = connection.orm<uuid_models::Schema>();
    context.createTable<uuid_models::Tag>();
    context.createTable<uuid_models::Owner>();
    context.createRelationTables<uuid_models::Owner>();
    const uuid_models::Tag tag{pattern(1)};
    const uuid_models::Owner owner{pattern(2), tag, {}};
    context.insert(tag);
    context.insert(owner);
    EXPECT_EQ(context.link<&uuid_models::Owner::tags>(owner, tag), 1U);
    for (const auto joined : {true, false})
    {
        orm::Query<uuid_models::Owner> query;
        query.where(col<&uuid_models::Owner::id>() == owner.id).include<&uuid_models::Owner::tags>();
        if (!joined)
            query.disableJoining();
        const auto owners = context.select(query);
        ASSERT_EQ(owners.size(), 1U);
        ASSERT_TRUE(owners.front().target);
        EXPECT_EQ(owners.front().target->id, tag.id);
        ASSERT_EQ(owners.front().tags.size(), 1U);
        EXPECT_EQ(owners.front().tags[0].id, tag.id);
    }
    EXPECT_EQ(context.unlink<&uuid_models::Owner::tags>(owner, tag), 1U);
}

} // namespace
