module;

#include <chrono>
#include <cstddef>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>

module orm;

import :value_codec;

namespace
{
using namespace std::chrono;

[[noreturn]] auto invalidValue() -> void
{
    throw std::invalid_argument{"Invalid date, time or UUID value"};
}

auto number(std::string_view digits) -> unsigned int
{
    unsigned int result{};
    for (const auto character : digits)
    {
        if (character < '0' || character > '9')
            invalidValue();
        result = result * 10U + static_cast<unsigned int>(character - '0');
    }
    return result;
}

auto requireNative(orm::db::BackendType backend) -> void
{
    if (backend != orm::db::BackendType::Postgres && backend != orm::db::BackendType::Mysql &&
        backend != orm::db::BackendType::Mariadb)
        invalidValue();
}

constexpr auto firstDay = year{1} / January / 1;
constexpr auto afterLastDay = year{10000} / January / 1;
}

namespace orm::detail::values
{
using namespace std::chrono;
auto valid(Date value) noexcept -> bool
{
    return value.ok() && value.year() >= std::chrono::year{1} && value.year() <= std::chrono::year{9999};
}

auto valid(TimeOfDay value) noexcept -> bool
{
    return value.sinceMidnight() >= microseconds::zero() && value.sinceMidnight() < hours{24};
}

auto valid(LocalDateTime value) noexcept -> bool
{
    return value >= local_days{firstDay} && value < local_days{afterLastDay};
}

auto valid(UtcTimestamp value) noexcept -> bool
{
    return value >= sys_days{firstDay} && value < sys_days{afterLastDay};
}

auto valid(const Uuid&) noexcept -> bool
{
    return true;
}

auto validate(Date value) -> void
{
    if (!valid(value))
        invalidValue();
}

// TimeOfDay preserves its domain invariant in every constructor.
auto validate(TimeOfDay) -> void {}

auto validate(LocalDateTime value) -> void
{
    if (!valid(value))
        invalidValue();
}

auto validate(UtcTimestamp value) -> void
{
    if (!valid(value))
        invalidValue();
}

auto validate(const Uuid&) -> void {}

auto format(Date value) -> std::string
{
    validate(value);
    return std::format("{:04}-{:02}-{:02}", static_cast<int>(value.year()), static_cast<unsigned int>(value.month()),
                       static_cast<unsigned int>(value.day()));
}

auto format(TimeOfDay value) -> std::string
{
    const hh_mm_ss<microseconds> parts{value.sinceMidnight()};
    return std::format("{:02}:{:02}:{:02}.{:06}", parts.hours().count(), parts.minutes().count(),
                       parts.seconds().count(), parts.subseconds().count());
}

auto format(LocalDateTime value) -> std::string
{
    validate(value);
    const auto day = floor<days>(value);
    return format(Date{day}) + " " + format(TimeOfDay{value - day});
}

auto format(UtcTimestamp value, bool includeOffset) -> std::string
{
    validate(value);
    return format(LocalDateTime{value.time_since_epoch()}) + (includeOffset ? "+00:00" : "");
}

auto format(const Uuid& value) -> std::string
{
    return value.toString();
}

auto parseDate(std::string_view text) -> Date
{
    if (text.size() != 10 || text[4] != '-' || text[7] != '-')
        invalidValue();
    const Date result{year{static_cast<int>(number(text.substr(0, 4)))}, month{number(text.substr(5, 2))},
                      day{number(text.substr(8, 2))}};
    validate(result);
    return result;
}

auto parseTimeOfDay(std::string_view text) -> TimeOfDay
{
    if (text.size() < 8 || text.size() > 15 || text[2] != ':' || text[5] != ':')
        invalidValue();
    const auto hour = number(text.substr(0, 2));
    const auto minute = number(text.substr(3, 2));
    const auto second = number(text.substr(6, 2));
    if (hour >= 24 || minute >= 60 || second >= 60)
        invalidValue();
    unsigned int fraction{};
    if (text.size() != 8)
    {
        if (text.size() == 9 || text[8] != '.')
            invalidValue();
        const auto digits = text.size() - 9;
        fraction = number(text.substr(9, digits));
        for (auto index = digits; index < 6; ++index)
            fraction *= 10U;
    }
    return TimeOfDay{hours{hour} + minutes{minute} + seconds{second} + microseconds{fraction}};
}

auto parseLocalDateTime(std::string_view text) -> LocalDateTime
{
    if (text.size() < 19 || (text[10] != ' ' && text[10] != 'T'))
        invalidValue();
    return local_days{parseDate(text.substr(0, 10))} + parseTimeOfDay(text.substr(11)).sinceMidnight();
}

auto parseUtcTimestamp(std::string_view text) -> UtcTimestamp
{
    if (text.size() < 19)
        invalidValue();
    const auto zone = text.find_first_of("Z+-", 19);
    const auto local = parseLocalDateTime(text.substr(0, zone));
    auto result = UtcTimestamp{local.time_since_epoch()};
    if (zone != std::string_view::npos)
    {
        const auto suffix = text.substr(zone);
        if (suffix != "Z")
        {
            if ((suffix.size() != 3 && suffix.size() != 6) || (suffix[0] != '+' && suffix[0] != '-') ||
                (suffix.size() == 6 && suffix[3] != ':'))
                invalidValue();
            const auto hour = number(suffix.substr(1, 2));
            const auto minute = suffix.size() == 6 ? number(suffix.substr(4, 2)) : 0U;
            if (hour > 23 || minute > 59)
                invalidValue();
            const auto offset = hours{hour} + minutes{minute};
            result += suffix[0] == '+' ? -offset : offset;
        }
    }
    validate(result);
    return result;
}

auto validateNative(Date value, db::BackendType backend) -> void
{
    requireNative(backend);
    validate(value);
    if (backend != db::BackendType::Postgres && value.year() < year{1000})
        invalidValue();
}

auto validateNative(TimeOfDay value, db::BackendType backend) -> void
{
    requireNative(backend);
    validate(value);
}

auto validateNative(LocalDateTime value, db::BackendType backend) -> void
{
    requireNative(backend);
    validate(value);
    validateNative(Date{floor<days>(value)}, backend);
    if (backend == db::BackendType::Mysql && value > local_days{afterLastDay} - seconds{1} + microseconds{499999})
        invalidValue();
}

auto validateNative(UtcTimestamp value, db::BackendType backend) -> void
{
    requireNative(backend);
    validate(value);
    if (backend != db::BackendType::Postgres)
    {
        const auto maximumTimestamp =
            backend == db::BackendType::Mysql ?
                sys_days{year{2038} / January / 19} + hours{3} + minutes{14} + seconds{7} + microseconds{499999} :
                sys_days{year{2106} / February / 7} + hours{6} + minutes{28} + seconds{15} + microseconds{999999};
        if (value < sys_days{year{1970} / January / 1} + seconds{1} || value > maximumTimestamp)
            invalidValue();
    }
}

auto validateNativeUuid(const Uuid& value, db::BackendType backend) -> void
{
    if (backend != db::BackendType::Postgres && backend != db::BackendType::Mariadb)
        invalidValue();
    if (backend == db::BackendType::Mariadb && (std::to_integer<unsigned int>(value.bytes()[6]) >> 4U) >= 8U &&
        (std::to_integer<unsigned int>(value.bytes()[8]) & 0x80U) == 0U)
        invalidValue();
}

auto validateNative(const Uuid& value, db::BackendType backend) -> void
{
    validateNativeUuid(value, backend);
}
}
