module;

#include "tests/UnitTestPrelude.hpp"

module orm;

import :value_codec;
import :foundation;
import :expressions;
import :sql;

namespace
{
using namespace orm::detail::values;
using namespace std::chrono_literals;

TEST(ValueCodecTest, roundTripsCivilDatesTimesAndMicrosecondsWithoutLocale)
{
    EXPECT_EQ(format(parseDate("2024-02-29")), "2024-02-29");
    EXPECT_EQ(format(parseTimeOfDay("23:59:59.000001")), "23:59:59.000001");
    EXPECT_EQ(format(parseTimeOfDay("00:00:00")), "00:00:00.000000");
    EXPECT_EQ(format(parseTimeOfDay("12:34:56.1")), "12:34:56.100000");
    EXPECT_EQ(format(parseLocalDateTime("2026-10-25T12:34:56.123456")), "2026-10-25 12:34:56.123456");
    EXPECT_EQ(format(parseLocalDateTime("0001-01-01 00:00:00")), "0001-01-01 00:00:00.000000");
    EXPECT_EQ(format(parseLocalDateTime("9999-12-31 23:59:59.999999")), "9999-12-31 23:59:59.999999");
    EXPECT_EQ(format(parseLocalDateTime("1969-12-31 23:59:59.999999")), "1969-12-31 23:59:59.999999");
}

TEST(ValueCodecTest, normalizesNumericOffsetsWithoutUsingTheHostTimeZone)
{
    const auto instant = parseUtcTimestamp("2026-10-25 00:30:00.123456+00:00");
    EXPECT_EQ(parseUtcTimestamp("2026-10-25 02:30:00.123456+02:00"), instant);
    EXPECT_EQ(parseUtcTimestamp("2026-10-24 19:30:00.123456-05"), instant);
    EXPECT_EQ(parseUtcTimestamp("2026-10-25 00:30:00.123456Z"), instant);
    EXPECT_EQ(parseUtcTimestamp("2026-10-25 00:30:00.123456+00"), instant);
    EXPECT_EQ(parseUtcTimestamp("2026-10-25 00:30:00.123456"), instant);
    EXPECT_EQ(format(instant), "2026-10-25 00:30:00.123456+00:00");
    EXPECT_EQ(format(instant, false), "2026-10-25 00:30:00.123456");
}

TEST(ValueCodecTest, rejectsInvalidCalendarValuesAndLossOfPrecision)
{
    for (const auto text : {"", "0000-01-01", "2023-02-29", "2024-00-01", "2024-01-00", "10000-01-01", "infinity",
                            "2024/02/29", "2024-02-29 BC"})
        EXPECT_THROW((void)parseDate(text), std::invalid_argument) << text;
    for (const auto text : {"", "24:00:00", "-01:00:00", "12:60:00", "12:00:60", "00:00:00.", "00:00:00.1234567",
                            "00:00:00x123", "00:00:0x"})
        EXPECT_THROW((void)parseTimeOfDay(text), std::invalid_argument) << text;
    for (const auto text : {"2024-01-01", "2024-01-01X12:00:00", "2024-01-01 12:00:00+00"})
        EXPECT_THROW((void)parseLocalDateTime(text), std::invalid_argument) << text;
    for (const auto text :
         {"", "infinity", "2024-01-01 00:00:00+24", "2024-01-01 00:00:00+00:60", "2024-01-01 00:00:00+0000",
          "2024-01-01 00:00:00Zjunk", "0001-01-01 00:00:00+01", "9999-12-31 23:59:59-01"})
        EXPECT_THROW((void)parseUtcTimestamp(text), std::invalid_argument) << text;
    EXPECT_FALSE(valid(orm::Date{std::chrono::year{2023}, std::chrono::February, std::chrono::day{29}}));
    EXPECT_FALSE(valid(orm::LocalDateTime{(std::chrono::microseconds::max)()}));
    EXPECT_FALSE(valid(orm::UtcTimestamp{(std::chrono::microseconds::min)()}));
}

TEST(ValueCodecTest, validatesNativeRangesAndMariaDbUuidRules)
{
    using enum orm::db::BackendType;
    EXPECT_NO_THROW(validateNative(parseDate("0001-01-01"), Postgres));
    EXPECT_THROW(validateNative(parseDate("0999-12-31"), Mysql), std::invalid_argument);
    EXPECT_THROW(validateNative(parseDate("0999-12-31"), Mariadb), std::invalid_argument);
    EXPECT_THROW(validateNative(parseDate("2026-10-25"), Sqlite), std::invalid_argument);
    EXPECT_THROW(validateNative(parseUtcTimestamp("1970-01-01 00:00:00"), Mysql), std::invalid_argument);
    EXPECT_NO_THROW(validateNative(parseUtcTimestamp("2038-01-19 03:14:07.499999"), Mysql));
    EXPECT_THROW(validateNative(parseUtcTimestamp("2038-01-19 03:14:07.500000"), Mysql), std::invalid_argument);
    EXPECT_NO_THROW(validateNative(parseUtcTimestamp("2106-02-07 06:28:15.999999"), Mariadb));
    EXPECT_THROW(validateNative(parseUtcTimestamp("2106-02-07 06:28:16"), Mariadb), std::invalid_argument);
    const auto pattern = orm::Uuid::parse("12345678-1234-8123-0123-123456789abc");
    EXPECT_NO_THROW(validateNative(pattern, Postgres));
    EXPECT_THROW(validateNative(pattern, Mariadb), std::invalid_argument);
    EXPECT_NO_THROW(validateNative(orm::Uuid{}, Mariadb));
    EXPECT_THROW(validateNative(orm::Uuid{}, Mysql), std::invalid_argument);
}

TEST(ValueCodecTest, preservesLogicalTypesAndIntersectsSessionTimestampBounds)
{
    const auto instant = parseUtcTimestamp("2040-01-01 00:00:00.123456");
    const orm::query::QueryValue value{instant};
    EXPECT_EQ(value.getLogicalType(), orm::model::ColumnType::UtcTimestamp);
    EXPECT_EQ(std::get<orm::UtcTimestamp>(value.get()), instant);
    EXPECT_FALSE(orm::query::QueryValue::isCompatibleStorage(orm::model::ColumnType::LocalDateTime, value.get()));
    const orm::db::BoundValue bound{value.getLogicalType(), value.get()};
    orm::db::BackendValueLimits limits;
    limits.maxUtcTimestamp = parseUtcTimestamp("2106-02-07 06:28:15.999999");
    orm::db::BackendRuntimeLimits session;
    session.maxUtcTimestamp = parseUtcTimestamp("2038-01-19 03:14:07.999999");
    EXPECT_TRUE(orm::db::binding::isWithinValueLimits(bound, limits));
    EXPECT_FALSE(orm::db::binding::isWithinValueLimits(bound, limits, session));
    EXPECT_FALSE(orm::db::binding::isWithinValueLimits(
        {orm::model::ColumnType::Date, orm::query::QueryValue::Value{std::string{"2026-10-25"}}}, limits));
}

TEST(ValueCodecTest, appliesInclusiveCivilAndUnsignedBoundsToTheirCanonicalStorage)
{
    orm::db::BackendValueLimits limits;
    limits.minDate = parseDate("2024-01-01");
    limits.maxDate = parseDate("2024-12-31");
    limits.minLocalDateTime = parseLocalDateTime("2024-01-01 00:00:00.000001");
    limits.maxLocalDateTime = parseLocalDateTime("2024-12-31 23:59:59.999998");
    limits.maxUnsignedLongLong = 42;
    const auto accepts = [&](const auto& input)
    {
        const orm::query::QueryValue value{input};
        return orm::db::binding::isWithinValueLimits({value.getLogicalType(), value.get()}, limits);
    };

    EXPECT_TRUE(accepts(*limits.minDate));
    EXPECT_TRUE(accepts(*limits.maxDate));
    EXPECT_FALSE(accepts(parseDate("2023-12-31")));
    EXPECT_FALSE(accepts(parseDate("2025-01-01")));
    EXPECT_TRUE(accepts(*limits.minLocalDateTime));
    EXPECT_TRUE(accepts(*limits.maxLocalDateTime));
    EXPECT_FALSE(accepts(*limits.minLocalDateTime - 1us));
    EXPECT_FALSE(accepts(*limits.maxLocalDateTime + 1us));
    EXPECT_TRUE(accepts(42ULL));
    EXPECT_FALSE(accepts(43ULL));
    EXPECT_TRUE(accepts(orm::TimeOfDay{12h}));
    EXPECT_TRUE(accepts(orm::Uuid{}));
    EXPECT_TRUE(orm::db::binding::isWithinValueLimits({orm::model::ColumnType::Date, std::nullopt}, limits));
}
}
