module;

#include <array>
#include <chrono>
#include <compare>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

export module orm:foundation_values;

export namespace orm
{
/** A calendar date. Database operations validate the calendar and backend range. */
using Date = std::chrono::year_month_day;

/** A civil date and time without an associated time zone. */
using LocalDateTime = std::chrono::local_time<std::chrono::microseconds>;

/** An instant on the UTC timeline, with microsecond resolution. */
using UtcTimestamp = std::chrono::sys_time<std::chrono::microseconds>;

/** A time of day in the half-open interval [00:00:00, 24:00:00). */
class TimeOfDay
{
public:
    constexpr TimeOfDay() noexcept = default;

    explicit constexpr TimeOfDay(std::chrono::microseconds valueInit) : value{valueInit}
    {
        if (value < std::chrono::microseconds::zero() || value >= std::chrono::hours{24})
        {
            throw std::invalid_argument{"Time of day must be between midnight and the end of the day"};
        }
    }

    [[nodiscard]] constexpr auto sinceMidnight() const noexcept -> std::chrono::microseconds
    {
        return value;
    }

    constexpr auto operator<=>(const TimeOfDay&) const noexcept = default;

private:
    std::chrono::microseconds value{};
};

/**
 * A 128-bit UUID in canonical byte order, independent of a database backend.
 *
 * Default construction produces the nil UUID. Parsing accepts exactly the
 * 8-4-4-4-12 hexadecimal format, with either case; formatting uses lowercase.
 * Every 128-bit pattern is representable. Byte ordering in C++ does not promise
 * the same order as a database's native UUID comparisons.
 */
class Uuid
{
public:
    constexpr Uuid() noexcept = default;

    explicit constexpr Uuid(std::array<std::byte, 16> valueInit) noexcept : value{valueInit} {}

    [[nodiscard]] constexpr auto bytes() const noexcept -> const std::array<std::byte, 16>&
    {
        return value;
    }

    /** @throws std::invalid_argument if the input is not in the required format. */
    [[nodiscard]] static auto parse(std::string_view text) -> Uuid;

    [[nodiscard]] auto toString() const -> std::string;

    constexpr auto operator<=>(const Uuid&) const noexcept = default;

private:
    std::array<std::byte, 16> value{};
};
} // namespace orm
