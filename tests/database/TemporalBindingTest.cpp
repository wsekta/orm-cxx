module;

#include "tests/UnitTestPrelude.hpp"

module orm;

import :internal;
import :test_support;

using namespace orm::test::fixtures;
using namespace orm::query;

namespace temporal_binding_models
{
struct TemporalRecord
{
    int id;
    orm::Date date;
    std::optional<orm::TimeOfDay> time;
    orm::LocalDateTime local;
    std::optional<orm::UtcTimestamp> utc;

    inline static constexpr orm::reflection::FixedString table_name{"temporal_binding_records"};
};

struct TemporalProjection
{
    std::optional<orm::UtcTimestamp> instant;
    orm::LocalDateTime civil;
    std::optional<orm::TimeOfDay> clock;
    orm::Date day;
    int number;
};

struct WrongTemporalProjection
{
    std::string day;
};

struct TemporalOnlyProjection
{
    orm::LocalDateTime value;
};

struct TemporalGroupProjection
{
    orm::LocalDateTime value;
    long long occurrences;
};

struct TemporalAggregateProjection
{
    std::optional<orm::Date> firstDate;
    std::optional<orm::Date> lastDate;
    std::optional<orm::TimeOfDay> firstTime;
    std::optional<orm::TimeOfDay> lastTime;
    std::optional<orm::LocalDateTime> firstLocal;
    std::optional<orm::LocalDateTime> lastLocal;
    std::optional<orm::UtcTimestamp> firstUtc;
    std::optional<orm::UtcTimestamp> lastUtc;
};

struct IntegerOnly
{
    int id;
    inline static constexpr orm::reflection::FixedString table_name{"temporal_binding_integer_only"};
};

struct UuidSource
{
    int id;
    orm::Uuid value;
};

struct RelatedUuidSource
{
    int id;
    std::optional<UuidSource> target;
};

struct UuidPredicateChild;
struct UuidPredicateOwner
{
    int id;
    orm::OneToMany<UuidPredicateChild> children;
    inline static constexpr auto relations =
        orm::relations(orm::oneToMany<&UuidPredicateOwner::children>().mappedBy<"owner">());
};

struct UuidPredicateChild
{
    int id;
    orm::Uuid value;
    std::optional<UuidPredicateOwner> owner;
};

struct UuidChild;
struct UuidTag
{
    orm::Uuid id;
    orm::TimeOfDay time;

    inline static constexpr orm::reflection::FixedString table_name{"temporal_binding_tags"};
};

struct UuidOwner
{
    orm::Uuid id;
    orm::Date day;
    std::string name;
    orm::OneToMany<UuidChild> children;
    orm::ManyToMany<UuidTag> tags;

    inline static constexpr orm::reflection::FixedString table_name{"temporal_binding_owners"};
    inline static constexpr auto id_columns = orm::primaryKey<&UuidOwner::id, &UuidOwner::day>();
    inline static constexpr auto relations = orm::relations(orm::oneToMany<&UuidOwner::children>().mappedBy<"owner">(),
                                                            orm::manyToMany<&UuidOwner::tags>()
                                                                .through<"temporal_binding_owner_tags">()
                                                                .ownerColumns<"owner_id", "owner_day">()
                                                                .targetColumns<"tag_id">());
};

struct UuidChild
{
    orm::Uuid id;
    std::optional<UuidOwner> owner;
    orm::LocalDateTime local;

    inline static constexpr orm::reflection::FixedString table_name{"temporal_binding_children"};
};

using Schema = orm::Schema<TemporalRecord, UuidOwner, UuidChild, UuidTag, IntegerOnly, UuidSource, RelatedUuidSource,
                           UuidPredicateOwner, UuidPredicateChild>;
} // namespace temporal_binding_models

namespace
{
namespace temporal_models = temporal_binding_models;

auto temporalRecord(int id, int fraction, bool present = true) -> temporal_models::TemporalRecord
{
    using namespace std::chrono_literals;
    const orm::Date date{std::chrono::year{2026}, std::chrono::October, std::chrono::day{25}};
    const auto microseconds = std::chrono::microseconds{fraction};
    return {id, date, present ? std::optional{orm::TimeOfDay{12h + 34min + 56s + microseconds}} : std::nullopt,
            orm::LocalDateTime{std::chrono::local_days{date}.time_since_epoch() + 12h + 34min + 56s + microseconds},
            present ? std::optional{orm::UtcTimestamp{std::chrono::sys_days{date}.time_since_epoch() + 12h + 34min +
                                                      56s + microseconds}} :
                      std::nullopt};
}
} // namespace

class TemporalBindingDatabaseTest : public DatabaseTest<temporal_models::Schema>
{
protected:
    auto supportsTemporal() const -> bool
    {
        return connection.getBackendCapabilities().supportsColumnType(orm::model::ColumnType::LocalDateTime);
    }
    auto supportsUuid() const -> bool
    {
        return connection.getBackendCapabilities().supportsColumnType(orm::model::ColumnType::Uuid);
    }
};

TEST_P(TemporalBindingDatabaseTest, nativeRowsRetainMicrosecondsAcrossNullableFetchAndProjectionOrder)
{
    if (!supportsTemporal())
        GTEST_SKIP() << "This backend has no native temporal columns";
    createTable<temporal_models::TemporalRecord>();
    const auto first = temporalRecord(1, 123456);
    const auto second = temporalRecord(2, 1, false);
    const auto third = temporalRecord(3, 123457);
    database.insert(std::vector{first, second, third});

    orm::Query<temporal_models::TemporalRecord> query;
    query.orderBy(asc(col<&temporal_models::TemporalRecord::id>()));
    const auto rows = database.select(query);
    ASSERT_EQ(rows.size(), 3);
    EXPECT_EQ(rows[0].date, first.date);
    EXPECT_EQ(rows[0].time, first.time);
    EXPECT_EQ(rows[0].local, first.local);
    EXPECT_EQ(rows[0].utc, first.utc);
    EXPECT_FALSE(rows[1].time);
    EXPECT_FALSE(rows[1].utc);
    EXPECT_EQ(rows[1].local, second.local);
    EXPECT_EQ(rows[2].time, third.time);
    EXPECT_EQ(rows[2].local - rows[0].local, std::chrono::microseconds{1});

    orm::ProjectionQuery<temporal_models::TemporalRecord, temporal_models::TemporalProjection> projection;
    projection
        .project(as("number", col<&temporal_models::TemporalRecord::id>()),
                 as("day", col<&temporal_models::TemporalRecord::date>()),
                 as("clock", col<&temporal_models::TemporalRecord::time>()),
                 as("civil", col<&temporal_models::TemporalRecord::local>()),
                 as("instant", col<&temporal_models::TemporalRecord::utc>()))
        .orderBy(asc(col<&temporal_models::TemporalRecord::id>()));
    const auto projected = database.select(projection);
    ASSERT_EQ(projected.size(), 3);
    EXPECT_EQ(projected[0].instant, first.utc);
    EXPECT_EQ(projected[0].civil, first.local);
    EXPECT_EQ(projected[0].clock, first.time);
    EXPECT_EQ(projected[0].day, first.date);
    EXPECT_EQ(projected[0].number, first.id);
    EXPECT_FALSE(projected[1].instant);
    EXPECT_FALSE(projected[1].clock);

    orm::ProjectionQuery<temporal_models::TemporalRecord, temporal_models::WrongTemporalProjection> incompatible;
    incompatible.project(as("day", col<&temporal_models::TemporalRecord::date>()));
    try
    {
        (void)database.select(incompatible);
        FAIL() << "Expected incompatible temporal projection to fail";
    }
    catch (const orm::DatabaseError& error)
    {
        EXPECT_EQ(error.getCode(), orm::DatabaseErrorCode::Conversion);
    }
}

TEST_P(TemporalBindingDatabaseTest, compiledTemporalSelectAndMutationShareTheSameLosslessBinding)
{
    if (!supportsTemporal())
        GTEST_SKIP() << "This backend has no native temporal columns";
    createTable<temporal_models::TemporalRecord>();
    const auto initial = temporalRecord(1, 1);
    const auto replacement = temporalRecord(1, 123456);
    database.insert(initial);

    constexpr auto lookup = select<temporal_models::TemporalRecord>().where(
        col<&temporal_models::TemporalRecord::local>() == param<orm::LocalDateTime, 0>());
    auto rows = database.select(lookup, initial.local);
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows.front().local, initial.local);
    EXPECT_EQ(rows.front().time, initial.time);

    constexpr auto change = orm::query::update<temporal_models::TemporalRecord>()
                                .set(col<&temporal_models::TemporalRecord::local>(), param<orm::LocalDateTime, 0>())
                                .set(col<&temporal_models::TemporalRecord::utc>(), param<orm::UtcTimestamp, 1>())
                                .where(col<&temporal_models::TemporalRecord::id>() == param<int, 2>());
    EXPECT_EQ(database.update(change, replacement.local, *replacement.utc, 1), 1);
    rows = database.select(lookup, replacement.local);
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows.front().utc, replacement.utc);

    constexpr auto projected = selectAs<temporal_models::TemporalRecord, temporal_models::TemporalProjection>(
        as<"number">(col<&temporal_models::TemporalRecord::id>()),
        as<"day">(col<&temporal_models::TemporalRecord::date>()),
        as<"clock">(col<&temporal_models::TemporalRecord::time>()),
        as<"civil">(col<&temporal_models::TemporalRecord::local>()),
        as<"instant">(col<&temporal_models::TemporalRecord::utc>()));
    const auto projections = database.select(projected);
    ASSERT_EQ(projections.size(), 1);
    EXPECT_EQ(projections.front().civil, replacement.local);
    EXPECT_EQ(projections.front().instant, replacement.utc);
}

TEST_P(TemporalBindingDatabaseTest, uuidCompositeKeysHydrateToOneAndCollectionsWithBothJoinModes)
{
    if (!supportsUuid())
        GTEST_SKIP() << "This backend has no native UUID columns";
    createTable<temporal_models::UuidOwner>();
    createTable<temporal_models::UuidChild>();
    createTable<temporal_models::UuidTag>();
    createRelationTables<temporal_models::UuidOwner>();
    const auto date = temporalRecord(1, 1).date;
    const temporal_models::UuidOwner owner{
        orm::Uuid::parse("00112233-4455-4677-8899-aabbccddeeff"), date, "owner", {}, {}};
    const temporal_models::UuidChild child{orm::Uuid::parse("10213243-5465-4767-8899-aabbccddeeff"), std::nullopt,
                                           temporalRecord(1, 123456).local};
    const temporal_models::UuidTag tag{orm::Uuid::parse("20314253-6475-4867-8899-aabbccddeeff"),
                                       orm::TimeOfDay{std::chrono::hours{12} + std::chrono::minutes{34} +
                                                      std::chrono::seconds{56} + std::chrono::microseconds{1}}};
    database.insert(owner);
    database.insert(child);
    database.insert(tag);
    EXPECT_EQ(database.link<&temporal_models::UuidOwner::children>(owner, child), 1);
    EXPECT_EQ(database.link<&temporal_models::UuidOwner::tags>(owner, tag), 1);

    for (const auto joined : {true, false})
    {
        orm::Query<temporal_models::UuidOwner> owners;
        owners.where(col<&temporal_models::UuidOwner::id>() == owner.id)
            .include<&temporal_models::UuidOwner::children>()
            .include<&temporal_models::UuidOwner::tags>();
        if (!joined)
            owners.disableJoining();
        const auto rows = database.select(owners);
        ASSERT_EQ(rows.size(), 1);
        EXPECT_EQ(rows.front().id, owner.id);
        EXPECT_EQ(rows.front().day, owner.day);
        ASSERT_EQ(rows.front().children.size(), 1);
        EXPECT_EQ(rows.front().children[0].id, child.id);
        EXPECT_EQ(rows.front().children[0].local, child.local);
        ASSERT_TRUE(rows.front().children[0].owner);
        EXPECT_EQ(rows.front().children[0].owner->id, owner.id);
        EXPECT_EQ(rows.front().children[0].owner->day, owner.day);
        ASSERT_EQ(rows.front().tags.size(), 1);
        EXPECT_EQ(rows.front().tags[0].id, tag.id);
        EXPECT_EQ(rows.front().tags[0].time, tag.time);
    }
    EXPECT_EQ(database.unlink<&temporal_models::UuidOwner::children>(owner, child), 1);
    EXPECT_EQ(database.unlink<&temporal_models::UuidOwner::tags>(owner, tag), 1);
    orm::Query<temporal_models::UuidChild> detached;
    const auto children = database.select(detached);
    ASSERT_EQ(children.size(), 1);
    EXPECT_FALSE(children.front().owner);
}

TEST_P(TemporalBindingDatabaseTest, distinctAndGroupingKeepValuesOneMicrosecondApart)
{
    if (!supportsTemporal())
        GTEST_SKIP() << "This backend has no native temporal columns";
    createTable<temporal_models::TemporalRecord>();
    database.insert(std::vector{temporalRecord(1, 1), temporalRecord(2, 1), temporalRecord(3, 2)});

    orm::ProjectionQuery<temporal_models::TemporalRecord, temporal_models::TemporalOnlyProjection> distinct;
    distinct.project(as("value", col<&temporal_models::TemporalRecord::local>()))
        .distinct()
        .orderBy(asc(col<&temporal_models::TemporalRecord::local>()));
    const auto unique = database.select(distinct);
    ASSERT_EQ(unique.size(), 2);
    EXPECT_EQ(unique[1].value - unique[0].value, std::chrono::microseconds{1});

    orm::ProjectionQuery<temporal_models::TemporalRecord, temporal_models::TemporalGroupProjection> grouped;
    grouped
        .project(as("value", col<&temporal_models::TemporalRecord::local>()),
                 as("occurrences", countAll<temporal_models::TemporalRecord>()))
        .groupBy(col<&temporal_models::TemporalRecord::local>())
        .having(min(col<&temporal_models::TemporalRecord::local>()) >= temporalRecord(1, 1).local)
        .orderBy(asc(col<&temporal_models::TemporalRecord::local>()));
    const auto groups = database.select(grouped);
    ASSERT_EQ(groups.size(), 2);
    EXPECT_EQ(groups[0].occurrences, 2);
    EXPECT_EQ(groups[1].occurrences, 1);
    EXPECT_EQ(groups[1].value - groups[0].value, std::chrono::microseconds{1});
}

TEST_P(TemporalBindingDatabaseTest, minAndMaxRetainEachTemporalTypeAndNullableEmptyResults)
{
    if (!supportsTemporal())
        GTEST_SKIP() << "This backend has no native temporal columns";
    createTable<temporal_models::TemporalRecord>();
    orm::ProjectionQuery<temporal_models::TemporalRecord, temporal_models::TemporalAggregateProjection> aggregate;
    aggregate.project(as("firstDate", min(col<&temporal_models::TemporalRecord::date>())),
                      as("lastDate", max(col<&temporal_models::TemporalRecord::date>())),
                      as("firstTime", min(col<&temporal_models::TemporalRecord::time>())),
                      as("lastTime", max(col<&temporal_models::TemporalRecord::time>())),
                      as("firstLocal", min(col<&temporal_models::TemporalRecord::local>())),
                      as("lastLocal", max(col<&temporal_models::TemporalRecord::local>())),
                      as("firstUtc", min(col<&temporal_models::TemporalRecord::utc>())),
                      as("lastUtc", max(col<&temporal_models::TemporalRecord::utc>())));
    auto rows = database.select(aggregate);
    ASSERT_EQ(rows.size(), 1);
    EXPECT_FALSE(rows.front().firstDate);
    EXPECT_FALSE(rows.front().lastDate);
    EXPECT_FALSE(rows.front().firstTime);
    EXPECT_FALSE(rows.front().lastTime);
    EXPECT_FALSE(rows.front().firstLocal);
    EXPECT_FALSE(rows.front().lastLocal);
    EXPECT_FALSE(rows.front().firstUtc);
    EXPECT_FALSE(rows.front().lastUtc);

    const auto first = temporalRecord(1, 1);
    const auto last = temporalRecord(2, 123456);
    database.insert(std::vector{first, last, temporalRecord(3, 123455, false)});
    rows = database.select(aggregate);
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows.front().firstDate, first.date);
    EXPECT_EQ(rows.front().lastDate, last.date);
    EXPECT_EQ(rows.front().firstTime, first.time);
    EXPECT_EQ(rows.front().lastTime, last.time);
    EXPECT_EQ(rows.front().firstLocal, first.local);
    EXPECT_EQ(rows.front().lastLocal, last.local);
    EXPECT_EQ(rows.front().firstUtc, first.utc);
    EXPECT_EQ(rows.front().lastUtc, last.utc);
}

TEST_P(TemporalBindingDatabaseTest, invalidDateFailsModelAndCompiledParameterConversion)
{
    if (!supportsTemporal())
        GTEST_SKIP() << "This backend has no native temporal columns";
    createTable<temporal_models::TemporalRecord>();
    auto value = temporalRecord(1, 1);
    value.date = orm::Date{std::chrono::year{2026}, std::chrono::February, std::chrono::day{30}};
    const auto expectConversion = [](auto operation)
    {
        try
        {
            operation();
            FAIL() << "Expected invalid calendar value conversion failure";
        }
        catch (const orm::DatabaseError& error)
        {
            EXPECT_EQ(error.getCode(), orm::DatabaseErrorCode::Conversion);
        }
    };
    expectConversion([&] { database.insert(value); });
    constexpr auto plan = select<temporal_models::TemporalRecord>().where(
        col<&temporal_models::TemporalRecord::date>() == param<orm::Date, 0>());
    expectConversion([&] { (void)database.select(plan, value.date); });
    orm::Query<temporal_models::TemporalRecord> query;
    EXPECT_TRUE(database.select(query).empty());
}

TEST_P(TemporalBindingDatabaseTest, unsupportedTypesInRawParametersAndCompiledModelsFailBeforeSql)
{
    createTable<temporal_models::IntegerOnly>();
    const auto expectUnsupported = [&](auto operation)
    {
        try
        {
            operation();
            FAIL() << "Expected unsupported native scalar type";
        }
        catch (const orm::DatabaseError& error)
        {
            EXPECT_EQ(error.getCode(), orm::DatabaseErrorCode::UnsupportedFeature);
        }
    };
    if (!supportsTemporal())
    {
        orm::Query<temporal_models::IntegerOnly> rawParameter;
        rawParameter.where(raw<temporal_models::IntegerOnly>("1 = :probe", param("probe", temporalRecord(1, 1).date)));
        expectUnsupported([&] { (void)database.select(rawParameter); });
        constexpr auto plan = select<temporal_models::TemporalRecord>().where(
            col<&temporal_models::TemporalRecord::id>() == param<int, 0>());
        expectUnsupported([&] { (void)database.select(plan, 1); });
    }
    if (!supportsUuid())
    {
        const auto uuid = orm::Uuid::parse("00112233-4455-4677-8899-aabbccddeeff");
        orm::Query<temporal_models::IntegerOnly> rawParameter;
        rawParameter.where(raw<temporal_models::IntegerOnly>("1 = :probe", param("probe", uuid)));
        expectUnsupported([&] { (void)database.select(rawParameter); });

        orm::ProjectionQuery<temporal_models::RelatedUuidSource, temporal_models::IntegerOnly> related;
        related.project(as("id", col<&temporal_models::RelatedUuidSource::id>()))
            .where(col<&temporal_models::RelatedUuidSource::target, &temporal_models::UuidSource::value>().isNotNull());
        expectUnsupported([&] { (void)database.select(related); });
        constexpr auto relatedPlan =
            selectAs<temporal_models::RelatedUuidSource, temporal_models::IntegerOnly>(
                as<"id">(col<&temporal_models::RelatedUuidSource::id>()))
                .where(col<&temporal_models::RelatedUuidSource::target, &temporal_models::UuidSource::value>()
                           .isNotNull());
        expectUnsupported([&] { (void)database.select(relatedPlan); });

        orm::Query<temporal_models::UuidPredicateOwner> collection;
        collection.where(any<&temporal_models::UuidPredicateOwner::children>(
            col<&temporal_models::UuidPredicateChild::value>().isNotNull()));
        expectUnsupported([&] { (void)database.select(collection); });
        constexpr auto collectionPlan =
            select<temporal_models::UuidPredicateOwner>().where(any<&temporal_models::UuidPredicateOwner::children>(
                col<&temporal_models::UuidPredicateChild::value>().isNotNull()));
        expectUnsupported([&] { (void)database.select(collectionPlan); });

        const auto collectionPredicate = any<&temporal_models::UuidPredicateOwner::children>(
            col<&temporal_models::UuidPredicateChild::value>().isNotNull());
        orm::Update<temporal_models::UuidPredicateOwner> change;
        change.set(col<&temporal_models::UuidPredicateOwner::id>(), 2).where(collectionPredicate);
        expectUnsupported([&] { (void)database.update(change); });
        expectUnsupported([&] { (void)database.remove<temporal_models::UuidPredicateOwner>(collectionPredicate); });

        constexpr auto changePlan = orm::query::update<temporal_models::UuidPredicateOwner>()
                                        .set(col<&temporal_models::UuidPredicateOwner::id>(), 2)
                                        .where(any<&temporal_models::UuidPredicateOwner::children>(
                                            col<&temporal_models::UuidPredicateChild::value>().isNotNull()));
        constexpr auto removePlan = orm::query::remove<temporal_models::UuidPredicateOwner>().where(
            any<&temporal_models::UuidPredicateOwner::children>(
                col<&temporal_models::UuidPredicateChild::value>().isNotNull()));
        expectUnsupported([&] { (void)database.update(changePlan); });
        expectUnsupported([&] { (void)database.remove(removePlan); });
    }
}

INSTANTIATE_TEST_SUITE_P(DatabaseTest, TemporalBindingDatabaseTest, backendTestConfigs, backendTestName);
