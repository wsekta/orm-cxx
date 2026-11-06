module;

#include "tests/UnitTestPrelude.hpp"

module orm;

import :internal;
import :migrations_internal;
import :test_support;

namespace mysql_binary_uuid_models
{
struct UserId
{
    orm::Uuid value;
    auto operator==(const UserId&) const -> bool = default;
};
} // namespace mysql_binary_uuid_models

template <>
struct orm::UuidAdapter<mysql_binary_uuid_models::UserId>
{
    static auto toUuid(const mysql_binary_uuid_models::UserId& value) -> orm::Uuid
    {
        return value.value;
    }
    static auto fromUuid(const orm::Uuid& value) -> mysql_binary_uuid_models::UserId
    {
        return {value};
    }
};

namespace mysql_binary_uuid_models
{
struct Record
{
    int id;
    UserId value;
    std::optional<UserId> optionalValue;
    inline static constexpr orm::reflection::FixedString table_name{"mysql_binary_uuid_records"};
    inline static constexpr auto uuid_storage = orm::uuidStorages(
        orm::uuidStorage<&Record::value, orm::db::BackendType::Mysql, orm::model::UuidStorage::Binary16>(),
        orm::uuidStorage<&Record::optionalValue, orm::db::BackendType::Mysql, orm::model::UuidStorage::Binary16>());
};
struct RecordDto
{
    int id;
    UserId value;
    std::optional<UserId> optionalValue;
};
struct Tag
{
    UserId id;
    inline static constexpr orm::reflection::FixedString table_name{"mysql_binary_uuid_tags"};
    inline static constexpr auto uuid_storage =
        orm::uuidStorages(orm::uuidStorage<&Tag::id, orm::db::BackendType::Mysql, orm::model::UuidStorage::Binary16>());
};
struct Owner
{
    UserId id;
    std::optional<Tag> target;
    orm::ManyToMany<Tag> tags;
    inline static constexpr orm::reflection::FixedString table_name{"mysql_binary_uuid_owners"};
    inline static constexpr auto uuid_storage = orm::uuidStorages(
        orm::uuidStorage<&Owner::id, orm::db::BackendType::Mysql, orm::model::UuidStorage::Binary16>());
    inline static constexpr auto relations = orm::relations(orm::manyToMany<&Owner::tags>()
                                                                .through<"mysql_binary_uuid_links">()
                                                                .ownerColumns<"owner_id">()
                                                                .targetColumns<"tag_id">());
};
using Schema = orm::Schema<Record, Tag, Owner>;
} // namespace mysql_binary_uuid_models

namespace
{
using namespace orm::query;
namespace binary_models = mysql_binary_uuid_models;

auto pattern(unsigned int group) -> binary_models::UserId
{
    std::array<std::byte, 16> bytes{};
    for (unsigned int index = 0; index < bytes.size(); ++index)
        bytes[index] = static_cast<std::byte>(group * 16 + index);
    return {orm::Uuid{bytes}};
}
} // namespace

class MysqlBinaryUuidTest : public DatabaseTest<binary_models::Schema>
{
};

TEST_P(MysqlBinaryUuidTest, CrudDynamicStaticAndDtoRoundTripEveryByte)
{
    createTable<binary_models::Record>();
    for (unsigned int group = 0; group < 16; ++group)
        database.insert(binary_models::Record{static_cast<int>(group), pattern(group),
                                              group == 0 ? std::nullopt : std::optional{pattern(group)}});
    const binary_models::UserId nil{orm::Uuid{}};
    database.insert(binary_models::Record{16, nil, nil});

    auto& session = orm::migrations::detail::DatabaseAccess::session(connection);
    int malformed{}, nulls{}, width{};
    std::string type;
    session << "SELECT COUNT(*) FROM `mysql_binary_uuid_records` WHERE OCTET_LENGTH(`value`) <> 16",
        soci::into(malformed);
    session << "SELECT COUNT(*) FROM `mysql_binary_uuid_records` WHERE `optionalValue` IS NULL", soci::into(nulls);
    session << "SELECT column_type,character_maximum_length FROM information_schema.columns "
               "WHERE table_schema=DATABASE() AND table_name='mysql_binary_uuid_records' AND column_name='value'",
        soci::into(type), soci::into(width);
    EXPECT_EQ(malformed, 0);
    EXPECT_EQ(nulls, 1);
    EXPECT_EQ(type, "binary(16)");
    EXPECT_EQ(width, 16);

    orm::Query<binary_models::Record> all;
    all.orderBy(asc(col<&binary_models::Record::id>()));
    const auto rows = database.select(all);
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

    orm::Query<binary_models::Record> dynamic;
    dynamic.where(col<&binary_models::Record::value>() == pattern(7));
    ASSERT_EQ(database.select(dynamic).size(), 1U);
    constexpr auto lookup = select<binary_models::Record>().where(col<&binary_models::Record::value>() ==
                                                                  param<binary_models::UserId, 0>());
    ASSERT_EQ(database.select(lookup, pattern(7)).size(), 1U);
    auto dynamicPlan = lookup.toDynamic(pattern(7));
    ASSERT_EQ(database.select(dynamicPlan).size(), 1U);

    orm::ProjectionQuery<binary_models::Record, binary_models::RecordDto> dto;
    dto.project(as("id", col<&binary_models::Record::id>()), as("value", col<&binary_models::Record::value>()),
                as("optionalValue", col<&binary_models::Record::optionalValue>()))
        .where(col<&binary_models::Record::id>() == 0);
    const auto projected = database.select(dto);
    ASSERT_EQ(projected.size(), 1U);
    EXPECT_EQ(projected.front().value, pattern(0));
    EXPECT_FALSE(projected.front().optionalValue);

    constexpr auto staticDto =
        selectAs<binary_models::Record, binary_models::RecordDto>(
            as<"id">(col<&binary_models::Record::id>()), as<"value">(col<&binary_models::Record::value>()),
            as<"optionalValue">(col<&binary_models::Record::optionalValue>()))
            .where(col<&binary_models::Record::id>() == param<int, 0>());
    const auto staticRows = database.select(staticDto, 7);
    ASSERT_EQ(staticRows.size(), 1U);
    EXPECT_EQ(staticRows.front().value, pattern(7));
    EXPECT_EQ(staticRows.front().optionalValue, pattern(7));

    orm::Update<binary_models::Record> update;
    update.set(col<&binary_models::Record::value>(), pattern(15)).where(col<&binary_models::Record::id>() == 0);
    EXPECT_EQ(database.update(update), 1U);
    EXPECT_EQ(database.remove<binary_models::Record>(col<&binary_models::Record::id>() == 15), 1U);
    orm::Query<binary_models::Record> changed;
    changed.where(col<&binary_models::Record::id>() == 0);
    const auto changedRows = database.select(changed);
    ASSERT_EQ(changedRows.size(), 1U);
    EXPECT_EQ(changedRows.front().value, pattern(15));
}

TEST_P(MysqlBinaryUuidTest, ShortExternalInputIsPaddedByMysqlBeforeItIsRead)
{
    createTable<binary_models::Record>();
    auto& session = orm::migrations::detail::DatabaseAccess::session(connection);
    session << "INSERT INTO `mysql_binary_uuid_records` (`id`,`value`,`optionalValue`) VALUES(1,X'0102',NULL)";
    std::string hex;
    int length{};
    session << "SELECT HEX(`value`),OCTET_LENGTH(`value`) FROM `mysql_binary_uuid_records` WHERE `id`=1",
        soci::into(hex), soci::into(length);
    EXPECT_EQ(hex, "01020000000000000000000000000000");
    EXPECT_EQ(length, 16);
    orm::Query<binary_models::Record> query;
    const auto rows = database.select(query);
    ASSERT_EQ(rows.size(), 1U);
    EXPECT_EQ(rows.front().value, (binary_models::UserId{orm::Uuid::parse("01020000-0000-0000-0000-000000000000")}));
    EXPECT_FALSE(rows.front().optionalValue);
}

TEST_P(MysqlBinaryUuidTest, BinaryKeysPropagateThroughToOneAndJunctionRelations)
{
    createTable<binary_models::Tag>();
    createTable<binary_models::Owner>();
    createRelationTables<binary_models::Owner>();
    const binary_models::Tag tag{pattern(1)};
    const binary_models::Owner owner{pattern(2), tag, {}};
    database.insert(tag);
    database.insert(owner);
    EXPECT_EQ(database.link<&binary_models::Owner::tags>(owner, tag), 1U);
    for (const auto joined : {true, false})
    {
        orm::Query<binary_models::Owner> query;
        query.where(col<&binary_models::Owner::id>() == owner.id).include<&binary_models::Owner::tags>();
        if (!joined)
            query.disableJoining();
        const auto owners = database.select(query);
        ASSERT_EQ(owners.size(), 1U);
        ASSERT_TRUE(owners.front().target);
        EXPECT_EQ(owners.front().target->id, tag.id);
        ASSERT_EQ(owners.front().tags.size(), 1U);
        EXPECT_EQ(owners.front().tags[0].id, tag.id);
    }
    EXPECT_EQ(database.unlink<&binary_models::Owner::tags>(owner, tag), 1U);
}

INSTANTIATE_TEST_SUITE_P(Mysql, MysqlBinaryUuidTest,
                         ::testing::Values(BackendTestConfig{
                             .type = orm::db::BackendType::Mysql, .name = "Mysql", .supported = true}),
                         orm::test::fixtures::backendTestName);
