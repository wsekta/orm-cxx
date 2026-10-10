module;

#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

export module orm:database_numeric;

export import :database_soci;

namespace orm::db::binding
{
template <typename ModelField, typename... Types>
concept IsOneOfTypes = (std::is_same_v<ModelField, Types> || ...);

template <typename ModelField>
concept SociConvertableToInt =
    IsOneOfTypes<ModelField, bool, std::int8_t, char, unsigned char, short, unsigned short> or
    (std::is_same_v<ModelField, long> and sizeof(long) <= sizeof(int));

template <typename ModelField>
concept SociConvertableToLongLong = std::is_same_v<ModelField, long> and sizeof(long) > sizeof(int);

template <typename ModelField>
concept SociConvertableToUnsignedLongLong = IsOneOfTypes<ModelField, unsigned int, unsigned long>;

template <typename ModelField>
concept SociConvertableToDouble = IsOneOfTypes<ModelField, float>;

template <typename ModelField>
concept SociDefaultSupported = IsOneOfTypes<ModelField, int, long long, unsigned long long, double, std::string>;

template <typename T>
struct OptionalValue
{
    using Type = T;
};

template <typename T>
struct OptionalValue<std::optional<T>>
{
    using Type = T;
};

template <typename T>
using optional_value_t = typename OptionalValue<std::remove_cv_t<T>>::Type;

template <typename SchemaType, typename ModelField>
concept SchemaModel = SchemaType::template contains<std::remove_cv_t<optional_value_t<ModelField>>>;
} // namespace orm::db::binding
namespace orm::db::binding
{
/**
 * @brief Internal signal for a result value that cannot be represented by the
 * requested model or projection field without loss.
 */
class ConversionError final : public std::runtime_error
{
public:
    explicit ConversionError(std::string message) : std::runtime_error{std::move(message)} {}
};
} // namespace orm::db::binding
namespace orm::db::binding
{
template <typename Result, typename Source>
    requires(std::is_integral_v<Result> and std::is_integral_v<Source> and
             not std::is_same_v<std::remove_cv_t<Result>, bool> and not std::is_same_v<std::remove_cv_t<Source>, bool>)
[[nodiscard]] constexpr auto isIntegralInRange(Source value) noexcept -> bool
{
    using result_t = std::remove_cv_t<Result>;
    using source_t = std::remove_cv_t<Source>;

    if constexpr (std::is_signed_v<result_t> == std::is_signed_v<source_t>)
    {
        if constexpr (std::numeric_limits<result_t>::digits >= std::numeric_limits<source_t>::digits)
        {
            return true;
        }
        else
        {
            return value >= static_cast<source_t>(std::numeric_limits<result_t>::lowest()) and
                   value <= static_cast<source_t>(std::numeric_limits<result_t>::max());
        }
    }
    else if constexpr (std::is_signed_v<source_t>)
    {
        if (value < 0)
        {
            return false;
        }

        if constexpr (std::numeric_limits<result_t>::digits >= std::numeric_limits<source_t>::digits)
        {
            return true;
        }
        else
        {
            using unsigned_source_t = std::make_unsigned_t<source_t>;
            return static_cast<unsigned_source_t>(value) <=
                   static_cast<unsigned_source_t>(std::numeric_limits<result_t>::max());
        }
    }
    else
    {
        if constexpr (std::numeric_limits<result_t>::digits >= std::numeric_limits<source_t>::digits)
        {
            return true;
        }
        else
        {
            return value <= static_cast<source_t>(std::numeric_limits<result_t>::max());
        }
    }
}

[[noreturn]] inline auto throwLossyNumericConversion(std::string_view fieldName) -> void
{
    throw ConversionError{"Cannot convert numeric field without data loss: " + std::string{fieldName}};
}

template <typename Result, typename Source>
    requires(std::is_arithmetic_v<Result> and std::is_arithmetic_v<Source>)
auto checkedNumericCast(Source value, std::string_view fieldName) -> Result
{
    if constexpr (std::is_floating_point_v<Source>)
    {
        if (not std::isfinite(value))
        {
            throwLossyNumericConversion(fieldName);
        }
    }

    if constexpr (std::is_integral_v<Result> and std::is_integral_v<Source>)
    {
        if constexpr (std::is_same_v<std::remove_cv_t<Result>, bool>)
        {
            if (value != 0 and value != 1)
            {
                throwLossyNumericConversion(fieldName);
            }
        }
        else if constexpr (not std::is_same_v<std::remove_cv_t<Source>, bool>)
        {
            if (not isIntegralInRange<Result>(value))
            {
                throwLossyNumericConversion(fieldName);
            }
        }

        return static_cast<Result>(value);
    }
    else if constexpr (std::is_integral_v<Result> and std::is_floating_point_v<Source>)
    {
        const auto numericValue = static_cast<long double>(value);
        const auto upperExclusive = std::ldexp(1.0L, std::numeric_limits<Result>::digits);
        const auto lowerInclusive = std::is_signed_v<Result> ? -upperExclusive : 0.0L;

        if (std::trunc(numericValue) != numericValue or numericValue < lowerInclusive or
            numericValue >= upperExclusive or
            (std::is_same_v<std::remove_cv_t<Result>, bool> and numericValue != 0.0L and numericValue != 1.0L))
        {
            throwLossyNumericConversion(fieldName);
        }

        return static_cast<Result>(value);
    }
    else if constexpr (std::is_floating_point_v<Result> and std::is_integral_v<Source>)
    {
        const auto numericValue = static_cast<long double>(value);

        if (numericValue < static_cast<long double>(std::numeric_limits<Result>::lowest()) or
            numericValue > static_cast<long double>(std::numeric_limits<Result>::max()))
        {
            throwLossyNumericConversion(fieldName);
        }

        if constexpr (not std::is_same_v<std::remove_cv_t<Source>, bool>)
        {
            using unsigned_source_t = std::make_unsigned_t<Source>;
            const auto unsignedValue = static_cast<unsigned_source_t>(value);
            auto magnitude = unsignedValue;

            if constexpr (std::is_signed_v<Source>)
            {
                if (value < 0)
                {
                    magnitude = unsigned_source_t{} - unsignedValue;
                }
            }

            if (magnitude != 0)
            {
                const auto significantBits = std::bit_width(magnitude) - std::countr_zero(magnitude);

                if (significantBits > std::numeric_limits<Result>::digits)
                {
                    throwLossyNumericConversion(fieldName);
                }
            }
        }

        return static_cast<Result>(value);
    }
    else if constexpr (std::is_floating_point_v<Result> and std::is_floating_point_v<Source> and
                       std::numeric_limits<Result>::digits < std::numeric_limits<Source>::digits)
    {
        if (value < static_cast<Source>(std::numeric_limits<Result>::lowest()) or
            value > static_cast<Source>(std::numeric_limits<Result>::max()))
        {
            throwLossyNumericConversion(fieldName);
        }

        const auto converted = static_cast<Result>(value);

        if (static_cast<Source>(converted) != value)
        {
            throwLossyNumericConversion(fieldName);
        }

        return converted;
    }
    else
    {
        return static_cast<Result>(value);
    }
}
} // namespace orm::db::binding
namespace orm::db::binding
{
template <typename Result>
    requires std::is_arithmetic_v<Result>
auto parseNumericValue(std::string_view value, std::string_view fieldName) -> Result
{
    const auto storedValue = std::string{value};

    try
    {
        if constexpr (std::is_floating_point_v<Result>)
        {
            double parsed{};
            const auto* const end = storedValue.data() + storedValue.size();
            const auto [parsedEnd, error] =
                std::from_chars(storedValue.data(), end, parsed, std::chars_format::general);

            if (error != std::errc{} or parsedEnd != end)
            {
                throw ConversionError{"Cannot hydrate numeric field: " + std::string{fieldName}};
            }

            return checkedNumericCast<Result>(parsed, fieldName);
        }
        else if constexpr (std::is_unsigned_v<Result>)
        {
            std::size_t parsedCharacters{};
            const auto firstNonWhitespace = storedValue.find_first_not_of(" \f\n\r\t\v");

            if (firstNonWhitespace != std::string::npos and storedValue[firstNonWhitespace] == '-')
            {
                throw ConversionError{"Cannot hydrate numeric field: " + std::string{fieldName}};
            }

            const auto parsed = std::stoull(storedValue, &parsedCharacters);

            if (parsedCharacters != storedValue.size())
            {
                throw ConversionError{"Cannot hydrate numeric field: " + std::string{fieldName}};
            }

            return checkedNumericCast<Result>(parsed, fieldName);
        }
        else
        {
            std::size_t parsedCharacters{};
            const auto parsed = std::stoll(storedValue, &parsedCharacters);

            if (parsedCharacters != storedValue.size())
            {
                throw ConversionError{"Cannot hydrate numeric field: " + std::string{fieldName}};
            }

            return checkedNumericCast<Result>(parsed, fieldName);
        }
    }
    catch (const ConversionError&)
    {
        throw;
    }
    catch (const std::exception&)
    {
        throw ConversionError{"Cannot hydrate numeric field: " + std::string{fieldName}};
    }
}

template <typename Result, typename Stored>
    requires(std::is_arithmetic_v<Result> and std::is_arithmetic_v<Stored>)
auto tryGetNumericValue(Result* result, const soci::values& values, const std::string& fieldName) -> bool
{
    try
    {
        *result = checkedNumericCast<Result>(values.get<Stored>(fieldName), fieldName);
        return true;
    }
    catch (const ConversionError&)
    {
        throw;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

template <typename Result>
    requires std::is_arithmetic_v<Result>
auto getNumericValue(const soci::values& values, const std::string& fieldName) -> Result
{
    auto convertStoredValue = [&values, &fieldName]<typename Stored>() -> Result
    { return checkedNumericCast<Result>(values.get<Stored>(fieldName), fieldName); };

    try
    {
        switch (values.get_properties(fieldName).get_data_type())
        {
        case soci::dt_integer:
            return convertStoredValue.template operator()<int>();
        case soci::dt_long_long:
            return convertStoredValue.template operator()<long long>();
        case soci::dt_unsigned_long_long:
            return convertStoredValue.template operator()<unsigned long long>();
        case soci::dt_double:
            return convertStoredValue.template operator()<double>();
        case soci::dt_string:
            return parseNumericValue<Result>(values.get<std::string>(fieldName), fieldName);
        default:
            throw ConversionError{"Cannot hydrate numeric field: " + fieldName};
        }
    }
    catch (const ConversionError&)
    {
        throw;
    }
    catch (const std::exception&)
    {
        // Locally assembled soci::values instances have no column properties.
        // Try the canonical SOCI arithmetic holders used by orm-cxx instead.
    }
    auto result = Result{};

    if (tryGetNumericValue<Result, int>(&result, values, fieldName) or
        tryGetNumericValue<Result, long long>(&result, values, fieldName) or
        tryGetNumericValue<Result, unsigned long long>(&result, values, fieldName) or
        tryGetNumericValue<Result, double>(&result, values, fieldName))
    {
        return result;
    }

    try
    {
        return parseNumericValue<Result>(values.get<std::string>(fieldName), fieldName);
    }
    catch (const ConversionError&)
    {
        throw;
    }
    catch (const std::exception&)
    {
        throw ConversionError{"Cannot hydrate numeric field: " + fieldName};
    }
}
} // namespace orm::db::binding
