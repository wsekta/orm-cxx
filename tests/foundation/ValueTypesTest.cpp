module;

#include "tests/UnitTestPrelude.hpp"

module orm;

import :foundation;

namespace
{
using namespace std::chrono_literals;

static_assert(std::same_as<orm::Date, std::chrono::year_month_day>);
static_assert(std::same_as<orm::LocalDateTime, std::chrono::local_time<std::chrono::microseconds>>);
static_assert(std::same_as<orm::UtcTimestamp, std::chrono::sys_time<std::chrono::microseconds>>);
static_assert(not std::same_as<orm::LocalDateTime, orm::UtcTimestamp>);
static_assert(not std::is_convertible_v<orm::LocalDateTime, orm::UtcTimestamp>);
static_assert(not std::is_convertible_v<orm::UtcTimestamp, orm::LocalDateTime>);
static_assert(not std::is_convertible_v<std::chrono::microseconds, orm::TimeOfDay>);
static_assert(not std::is_constructible_v<orm::TimeOfDay, std::chrono::nanoseconds>);
static_assert(not std::is_convertible_v<std::array<std::byte, 16>, orm::Uuid>);
static_assert(static_cast<int>(orm::model::ColumnType::Bool) == 0);
static_assert(static_cast<int>(orm::model::ColumnType::String) == 11);
static_assert(static_cast<int>(orm::model::ColumnType::Uuid) == 12);

constexpr auto midnight = orm::TimeOfDay{};
constexpr auto firstMicrosecond = orm::TimeOfDay{1us};
constexpr auto lastMicrosecond = orm::TimeOfDay{24h - 1us};
static_assert(midnight.sinceMidnight() == 0us);
static_assert(firstMicrosecond.sinceMidnight() == 1us);
static_assert(lastMicrosecond.sinceMidnight() == 86'399'999'999us);
static_assert(midnight == orm::TimeOfDay{0us});
static_assert(midnight < firstMicrosecond);
static_assert(firstMicrosecond < lastMicrosecond);
static_assert(noexcept(midnight.sinceMidnight()));

constexpr auto nilUuid = orm::Uuid{};
constexpr auto nonNilBytes = std::array<std::byte, 16>{std::byte{1}};
constexpr auto nonNilUuid = orm::Uuid{nonNilBytes};
static_assert(nilUuid == orm::Uuid{std::array<std::byte, 16>{}});
static_assert(nonNilUuid.bytes() == nonNilBytes);
static_assert(nilUuid < nonNilUuid);
static_assert(noexcept(orm::Uuid{nonNilBytes}));
static_assert(noexcept(nonNilUuid.bytes()));
static_assert(std::same_as<decltype(nonNilUuid.bytes()), const std::array<std::byte, 16>&>);

TEST(ValueTypesTest, temporalAliasesPreserveCalendarAndMicrosecondPrecision)
{
    const auto leapDay = orm::Date{std::chrono::year{2024}, std::chrono::February, std::chrono::day{29}};
    const auto invalidLeapDay = orm::Date{std::chrono::year{2023}, std::chrono::February, std::chrono::day{29}};
    const auto local = orm::LocalDateTime{1'234'567us};
    const auto utc = orm::UtcTimestamp{1'234'567us};

    EXPECT_TRUE(leapDay.ok());
    EXPECT_FALSE(invalidLeapDay.ok());
    EXPECT_EQ(local.time_since_epoch(), 1'234'567us);
    EXPECT_EQ(utc.time_since_epoch(), 1'234'567us);
    EXPECT_EQ((local + 1us).time_since_epoch(), 1'234'568us);
    EXPECT_EQ((utc + 1us).time_since_epoch(), 1'234'568us);
}

TEST(TimeOfDayTest, preservesEveryMicrosecondAtDayBoundaries)
{
    EXPECT_EQ(orm::TimeOfDay{}.sinceMidnight(), 0us);
    EXPECT_EQ(orm::TimeOfDay{1us}.sinceMidnight(), 1us);
    EXPECT_EQ(orm::TimeOfDay{24h - 1us}.sinceMidnight(), 86'399'999'999us);
}

TEST(TimeOfDayTest, rejectsValuesOutsideOneDay)
{
    for (const auto invalid : {-24h - 1us, -1us, 24h + 0us, 24h + 1us, (std::chrono::microseconds::min)(),
                               (std::chrono::microseconds::max)()})
    {
        SCOPED_TRACE(invalid.count());
        EXPECT_THROW((void)orm::TimeOfDay{invalid}, std::invalid_argument);
    }
}

TEST(TimeOfDayTest, comparesByElapsedTimeSinceMidnight)
{
    const auto earlier = orm::TimeOfDay{12h + 34min + 56s + 123'456us};
    const auto later = orm::TimeOfDay{earlier.sinceMidnight() + 1us};

    EXPECT_EQ(earlier, orm::TimeOfDay{12h + 34min + 56s + 123'456us});
    EXPECT_NE(earlier, later);
    EXPECT_LT(earlier, later);
    EXPECT_GT(later, earlier);
}

TEST(UuidTest, formatsAndParsesBytesInCanonicalOrder)
{
    const auto bytes = std::array<std::byte, 16>{std::byte{0x00}, std::byte{0x11}, std::byte{0x22}, std::byte{0x33},
                                                 std::byte{0x44}, std::byte{0x55}, std::byte{0x66}, std::byte{0x77},
                                                 std::byte{0x88}, std::byte{0x99}, std::byte{0xaa}, std::byte{0xbb},
                                                 std::byte{0xcc}, std::byte{0xdd}, std::byte{0xee}, std::byte{0xff}};
    const auto uuid = orm::Uuid{bytes};

    EXPECT_EQ(uuid.bytes(), bytes);
    EXPECT_EQ(uuid.toString(), "00112233-4455-6677-8899-aabbccddeeff");
    EXPECT_EQ(orm::Uuid::parse("00112233-4455-6677-8899-aabbccddeeff").bytes(), bytes);
    EXPECT_EQ(orm::Uuid::parse(uuid.toString()), uuid);
}

TEST(UuidTest, acceptsUppercaseHexAndEmitsLowercaseWithLeadingZeros)
{
    const auto uppercase = orm::Uuid::parse("00000ABC-00DE-00F0-ABCD-00000000EF01");
    const auto lowercase = orm::Uuid::parse("00000abc-00de-00f0-abcd-00000000ef01");

    EXPECT_EQ(uppercase, lowercase);
    EXPECT_EQ(uppercase.toString(), "00000abc-00de-00f0-abcd-00000000ef01");
}

TEST(UuidTest, representsNilMaximumAndUnrestrictedVersionAndVariantBits)
{
    EXPECT_EQ(orm::Uuid{}.toString(), "00000000-0000-0000-0000-000000000000");
    EXPECT_EQ(orm::Uuid::parse("00000000-0000-0000-0000-000000000000"), orm::Uuid{});

    const auto maximumBytes = std::array<std::byte, 16>{
        std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff},
        std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff},
        std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}};
    const auto maximum = orm::Uuid::parse("ffffffff-ffff-ffff-ffff-ffffffffffff");

    EXPECT_EQ(maximum.bytes(), maximumBytes);
    EXPECT_EQ(maximum.toString(), "ffffffff-ffff-ffff-ffff-ffffffffffff");

    for (const auto text : {"12345678-9abc-0def-0123-456789abcdef", "12345678-9abc-fdef-f123-456789abcdef"})
    {
        SCOPED_TRACE(text);
        EXPECT_EQ(orm::Uuid::parse(text).toString(), text);
    }
}

TEST(UuidTest, rejectsNonCanonicalLengthsAndDecorations)
{
    for (const auto text : {"", "00112233-4455-6677-8899-aabbccddeef", "00112233-4455-6677-8899-aabbccddeeff0",
                            "00112233445566778899aabbccddeeff", "{00112233-4455-6677-8899-aabbccddeeff}",
                            " 00112233-4455-6677-8899-aabbccddeeff", "00112233-4455-6677-8899-aabbccddeeff ",
                            "00112233-4455-6677-8899-aabbccddeeff\n", "0011223 -4455-6677-8899-aabbccddeeff"})
    {
        SCOPED_TRACE(text);
        EXPECT_THROW((void)orm::Uuid::parse(text), std::invalid_argument);
    }
}

TEST(UuidTest, rejectsInvalidHexAtEveryDigit)
{
    const auto canonical = std::string{"00112233-4455-6677-8899-aabbccddeeff"};

    for (auto index = std::size_t{0}; index < canonical.size(); ++index)
    {
        if (canonical[index] == '-')
        {
            continue;
        }

        auto invalid = canonical;
        invalid[index] = 'g';
        SCOPED_TRACE(index);
        EXPECT_THROW((void)orm::Uuid::parse(invalid), std::invalid_argument);
    }
}

TEST(UuidTest, requiresHyphensAtEveryCanonicalSeparator)
{
    for (const auto index : {8, 13, 18, 23})
    {
        auto invalid = std::string{"00112233-4455-6677-8899-aabbccddeeff"};
        invalid[static_cast<std::size_t>(index)] = '_';
        SCOPED_TRACE(index);
        EXPECT_THROW((void)orm::Uuid::parse(invalid), std::invalid_argument);
    }
}

TEST(UuidTest, rejectsEmbeddedAndTrailingNullBytes)
{
    auto embeddedNull = std::string{"00112233-4455-6677-8899-aabbccddeeff"};
    embeddedNull[4] = '\0';
    auto trailingNull = std::string{"00112233-4455-6677-8899-aabbccddeeff"};
    trailingNull.push_back('\0');

    EXPECT_THROW((void)orm::Uuid::parse(embeddedNull), std::invalid_argument);
    EXPECT_THROW((void)orm::Uuid::parse(trailingNull), std::invalid_argument);
}

TEST(UuidTest, comparesLexicographicallyByCanonicalBytes)
{
    const auto ordered = std::array{orm::Uuid::parse("00000000-0000-0000-0000-000000000001"),
                                    orm::Uuid::parse("00000000-0000-0000-0001-000000000000"),
                                    orm::Uuid::parse("00000000-0000-0001-0000-000000000000"),
                                    orm::Uuid::parse("00000000-0001-0000-0000-000000000000"),
                                    orm::Uuid::parse("00000001-0000-0000-0000-000000000000"),
                                    orm::Uuid::parse("00000100-0000-0000-0000-000000000000"),
                                    orm::Uuid::parse("80000000-0000-0000-0000-000000000000"),
                                    orm::Uuid::parse("ff000000-0000-0000-0000-000000000000")};

    for (auto index = std::size_t{1}; index < ordered.size(); ++index)
    {
        SCOPED_TRACE(index);
        EXPECT_LT(ordered[index - 1], ordered[index]);
        EXPECT_GT(ordered[index], ordered[index - 1]);
        EXPECT_NE(ordered[index - 1], ordered[index]);
    }
}

} // namespace
