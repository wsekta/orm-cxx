module;

#include "tests/StandardLibrary.hpp"

module orm;

import :internal;
import :foundation;
import :model;
import :expressions;
import :dynamic_query;
import :static_plan;
import :sql;
import :database;

#include "TemporalQueryModels.hpp"

using namespace orm::query;
using namespace temporal_query_models;

static_assert(std::same_as<typename decltype(col<&Record::date>())::Value, orm::Date>);
static_assert(std::same_as<typename decltype(col<&Record::time>())::Value, orm::TimeOfDay>);
static_assert(std::same_as<typename decltype(col<&Record::local>())::Value, orm::LocalDateTime>);
static_assert(std::same_as<typename decltype(col<&Record::instant>())::Value, orm::UtcTimestamp>);
static_assert(std::same_as<typename decltype(col<&Record::uuid>())::Value, orm::Uuid>);
static_assert(decltype(col<&Record::optionalDate>())::nullable);
static_assert(decltype(col<&Record::optionalTime>())::nullable);
static_assert(decltype(col<&Record::optionalLocal>())::nullable);
static_assert(decltype(col<&Record::optionalInstant>())::nullable);
static_assert(decltype(col<&Record::optionalUuid>())::nullable);
static_assert(detail::supportedScalar<orm::Date>);
static_assert(detail::supportedScalar<orm::TimeOfDay>);
static_assert(detail::supportedScalar<orm::LocalDateTime>);
static_assert(detail::supportedScalar<orm::UtcTimestamp>);
static_assert(detail::supportedScalar<orm::Uuid>);
static_assert(not detail::numericScalar<orm::Date>);
static_assert(not detail::numericScalar<orm::TimeOfDay>);
static_assert(not detail::compatibleValue<orm::Date, orm::UtcTimestamp>);
static_assert(not detail::compatibleValue<orm::LocalDateTime, std::string>);
static_assert(not detail::compatibleValue<orm::UtcTimestamp, std::chrono::sys_time<std::chrono::nanoseconds>>);
static_assert(not detail::compatibleValue<orm::TimeOfDay, std::chrono::microseconds>);
static_assert(not detail::ORM_QUERY_ORDERABLE<orm::Uuid>);

static_assert(std::same_as<typename decltype(min(col<&Record::date>()))::Value, orm::Date>);
static_assert(std::same_as<typename decltype(max(col<&Record::date>()))::Value, orm::Date>);
static_assert(std::same_as<typename decltype(min(col<&Record::time>()))::Value, orm::TimeOfDay>);
static_assert(std::same_as<typename decltype(max(col<&Record::time>()))::Value, orm::TimeOfDay>);
static_assert(std::same_as<typename decltype(min(col<&Record::local>()))::Value, orm::LocalDateTime>);
static_assert(std::same_as<typename decltype(max(col<&Record::local>()))::Value, orm::LocalDateTime>);
static_assert(std::same_as<typename decltype(min(col<&Record::instant>()))::Value, orm::UtcTimestamp>);
static_assert(std::same_as<typename decltype(max(col<&Record::instant>()))::Value, orm::UtcTimestamp>);
static_assert(decltype(min(col<&Record::date>()))::nullable);
static_assert(decltype(max(col<&Record::time>()))::nullable);
static_assert(decltype(min(col<&Record::local>()))::nullable);
static_assert(decltype(max(col<&Record::instant>()))::nullable);
static_assert(std::same_as<typename decltype(count(col<&Record::uuid>()))::Value, long long>);
static_assert(not decltype(count(col<&Record::uuid>()))::nullable);

inline constexpr auto typedTemporalParameters = select<Record>().where(
    (col<&Record::date>() == param<orm::Date, 0>()) && (col<&Record::time>() >= param<orm::TimeOfDay, 1>()) &&
    (col<&Record::local>() < param<orm::LocalDateTime, 2>()) &&
    (col<&Record::instant>() != param<orm::UtcTimestamp, 3>()) && (col<&Record::uuid>() == param<orm::Uuid, 4>()));
inline constexpr auto typedUuidList =
    select<Record>().where(col<&Record::uuid>().in(param<std::vector<orm::Uuid>, 0>()));
inline constexpr auto typedNullableTemporalUpdate =
    update<Record>()
        .set(col<&Record::optionalDate>(), param<std::optional<orm::Date>, 0>())
        .set(col<&Record::optionalTime>(), param<std::optional<orm::TimeOfDay>, 1>())
        .set(col<&Record::optionalLocal>(), param<std::optional<orm::LocalDateTime>, 2>())
        .set(col<&Record::optionalInstant>(), param<std::optional<orm::UtcTimestamp>, 3>())
        .set(col<&Record::optionalUuid>(), param<std::optional<orm::Uuid>, 4>())
        .where(col<&Record::id>() == param<int, 5>());

template <auto Member, typename Value>
auto instantiateTemporalOperators(Value value) -> void
{
    (void)(col<Member>() == value);
    (void)(col<Member>() != value);
    (void)(col<Member>() < value);
    (void)(col<Member>() <= value);
    (void)(col<Member>() > value);
    (void)(col<Member>() >= value);
    (void)col<Member>().in(std::array{value, value});
    (void)col<Member>().notIn(std::vector<Value>{value});
    (void)col<Member>().between(value, value);
    (void)col<Member>().notBetween(value, value);
    (void)asc(col<Member>());
    (void)desc(col<Member>());
    (void)(min(col<Member>()) <= value);
    (void)(max(col<Member>()) >= value);
    (void)(count(col<Member>()) > 0);
}

[[maybe_unused]] auto instantiateTemporalQueryApi(Context& database) -> void
{
    const auto date = orm::Date{std::chrono::year{2024}, std::chrono::February, std::chrono::day{29}};
    const auto time = orm::TimeOfDay{std::chrono::microseconds{123'456}};
    const auto local = orm::LocalDateTime{std::chrono::microseconds{1'234'567}};
    const auto instant = orm::UtcTimestamp{std::chrono::microseconds{1'234'567}};
    const auto uuid = orm::Uuid::parse("01234567-89ab-7def-8123-456789abcdef");
    instantiateTemporalOperators<&Record::date>(date);
    instantiateTemporalOperators<&Record::time>(time);
    instantiateTemporalOperators<&Record::local>(local);
    instantiateTemporalOperators<&Record::instant>(instant);

    orm::Query<Record> query;
    query.where((col<&Record::uuid>() == uuid) && (col<&Record::uuid>() != orm::Uuid{}))
        .andWhere(col<&Record::uuid>().in(std::vector<orm::Uuid>{uuid}))
        .andWhere(col<&Record::uuid>().notIn(std::array{orm::Uuid{}}))
        .andWhere(col<&Record::optionalDate>().isNull())
        .orWhere(col<&Record::optionalTime>().isNotNull())
        .andWhere(col<&Record::optionalLocal>() == nullptr)
        .orWhere(col<&Record::optionalInstant>() != std::nullopt)
        .andWhere(col<&Record::optionalUuid>().isNotNull())
        .orderBy(asc(col<&Record::date>()), desc(col<&Record::time>()), asc(col<&Record::local>()),
                 desc(col<&Record::instant>()))
        .groupBy(col<&Record::date>(), col<&Record::uuid>())
        .having(count(col<&Record::uuid>()) > 0)
        .andHaving(min(col<&Record::date>()) == date)
        .andHaving(max(col<&Record::instant>()) >= instant);
    (void)database.select(query);

    struct Stats
    {
        std::optional<orm::Date> minimumDate;
        std::optional<orm::TimeOfDay> maximumTime;
        std::optional<orm::LocalDateTime> minimumLocal;
        std::optional<orm::UtcTimestamp> maximumInstant;
        long long uuidCount;
    };
    orm::ProjectionQuery<Record, Stats> projection;
    projection.project(as("minimumDate", min(col<&Record::date>())), as("maximumTime", max(col<&Record::time>())),
                       as("minimumLocal", min(col<&Record::local>())),
                       as("maximumInstant", max(col<&Record::instant>())),
                       as("uuidCount", count(col<&Record::uuid>())));
    (void)database.select(projection);

    orm::Update<Record> change;
    change.set(col<&Record::date>(), date)
        .set(col<&Record::time>(), time)
        .set(col<&Record::local>(), local)
        .set(col<&Record::instant>(), instant)
        .set(col<&Record::uuid>(), uuid)
        .set(col<&Record::optionalDate>(), std::optional<orm::Date>{date})
        .set(col<&Record::optionalTime>(), std::optional<orm::TimeOfDay>{time})
        .set(col<&Record::optionalLocal>(), std::optional<orm::LocalDateTime>{local})
        .set(col<&Record::optionalInstant>(), std::optional<orm::UtcTimestamp>{instant})
        .set(col<&Record::optionalUuid>(), std::optional<orm::Uuid>{uuid})
        .where(col<&Record::id>() == 1);
    (void)database.update(change);
    change.set(col<&Record::optionalDate>(), std::nullopt)
        .set(col<&Record::optionalTime>(), std::nullopt)
        .set(col<&Record::optionalLocal>(), std::nullopt)
        .set(col<&Record::optionalInstant>(), std::nullopt)
        .set(col<&Record::optionalUuid>(), std::nullopt);
    (void)database.update(change);

    (void)database.select(typedTemporalParameters, date, time, local, instant, uuid);
    (void)database.select(typedUuidList, std::vector<orm::Uuid>{uuid});
    (void)database.update(typedNullableTemporalUpdate, std::optional<orm::Date>{date},
                          std::optional<orm::TimeOfDay>{time}, std::optional<orm::LocalDateTime>{local},
                          std::optional<orm::UtcTimestamp>{instant}, std::optional<orm::Uuid>{uuid}, 1);
    (void)database.update(typedNullableTemporalUpdate, std::optional<orm::Date>{}, std::optional<orm::TimeOfDay>{},
                          std::optional<orm::LocalDateTime>{}, std::optional<orm::UtcTimestamp>{},
                          std::optional<orm::Uuid>{}, 1);
    auto dynamicParameters = typedTemporalParameters.toDynamic(date, time, local, instant, uuid);
    (void)database.select(dynamicParameters);
}
