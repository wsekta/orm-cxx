module;

#include <array>
#include <chrono>
#include <compare>
#include <concepts>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

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

    constexpr auto operator==(const TimeOfDay& other) const noexcept -> bool
    {
        return value.count() == other.value.count();
    }

    constexpr auto operator<=>(const TimeOfDay& other) const noexcept -> std::strong_ordering
    {
        if (value.count() < other.value.count())
        {
            return std::strong_ordering::less;
        }
        if (value.count() > other.value.count())
        {
            return std::strong_ordering::greater;
        }
        return std::strong_ordering::equal;
    }

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

    constexpr auto operator==(const Uuid& other) const noexcept -> bool
    {
        for (auto index = std::size_t{0}; index < value.size(); ++index)
        {
            if (value[index] != other.value[index])
            {
                return false;
            }
        }
        return true;
    }

    constexpr auto operator<=>(const Uuid& other) const noexcept -> std::strong_ordering
    {
        for (auto index = std::size_t{0}; index < value.size(); ++index)
        {
            if (value[index] < other.value[index])
            {
                return std::strong_ordering::less;
            }
            if (value[index] > other.value[index])
            {
                return std::strong_ordering::greater;
            }
        }
        return std::strong_ordering::equal;
    }

private:
    std::array<std::byte, 16> value{};
};

/** Specialize for an application UUID type to convert explicitly to and from Uuid. */
template <typename T>
struct UuidAdapter;

template <typename T>
concept UuidAdaptable = requires(const T& value, const Uuid& uuid) {
    { UuidAdapter<T>::toUuid(value) } -> std::same_as<Uuid>;
    { UuidAdapter<T>::fromUuid(uuid) } -> std::same_as<T>;
};

/**
 * Specialize to map a third-party value type to a canonical ORM value.
 *
 * `Value` must be one of the canonical ORM scalar values. The ORM derives the
 * logical column type from it, so the converter cannot disagree with the value
 * it returns. Converters are opt-in and independent of the built-in scalar
 * types and of `UuidAdapter`.
 */
template <typename T>
struct ValueConverter;

template <typename T>
concept ValueConvertible = requires {
    typename ValueConverter<T>::Value;
    requires requires(const T& value, const typename ValueConverter<T>::Value& canonical) {
        { ValueConverter<T>::toValue(value) } -> std::same_as<typename ValueConverter<T>::Value>;
        { ValueConverter<T>::fromValue(canonical) } -> std::same_as<T>;
    };
};

/**
 * Specialize for an application nullable wrapper such as `boost::optional<T>`.
 *
 * `Value` is the contained value type. `hasValue`, `value`, `make` and `none`
 * let models, DTOs and parameters treat the wrapper like `std::optional<T>`
 * without the ORM depending on the wrapper's library.
 */
template <typename T>
struct NullableAdapter;

template <typename T>
concept NullableAdaptable = requires {
    typename NullableAdapter<T>::Value;
    requires requires(const T& value, typename NullableAdapter<T>::Value&& contained) {
        { NullableAdapter<T>::hasValue(value) } -> std::convertible_to<bool>;
        { NullableAdapter<T>::value(value) } -> std::convertible_to<const typename NullableAdapter<T>::Value&>;
        { NullableAdapter<T>::make(std::move(contained)) } -> std::same_as<T>;
        { NullableAdapter<T>::none() } -> std::same_as<T>;
    };
};

/** `std::optional` uses the same nullable contract as application wrappers. */
template <typename T>
struct NullableAdapter<std::optional<T>>
{
    using Value = T;

    static constexpr auto hasValue(const std::optional<T>& value) noexcept -> bool
    {
        return value.has_value();
    }
    static constexpr auto value(const std::optional<T>& source) -> const T&
    {
        return *source;
    }
    static auto make(const T& contained) -> std::optional<T>
    {
        return std::optional<T>{contained};
    }
    static auto make(T&& contained) -> std::optional<T>
    {
        return std::optional<T>{std::move(contained)};
    }
    static auto none() -> std::optional<T>
    {
        return std::nullopt;
    }
};

/** True when `T` converts through the canonical `Uuid` value. */
template <typename T>
concept UuidConvertible = ValueConvertible<T> && std::same_as<typename ValueConverter<T>::Value, Uuid>;

/** True when `T` carries a UUID domain, whether natively or explicitly converted. */
template <typename T>
concept UuidDomainValue = std::same_as<std::remove_cvref_t<T>, Uuid> || UuidAdaptable<std::remove_cvref_t<T>> ||
                          UuidConvertible<std::remove_cvref_t<T>>;
} // namespace orm
