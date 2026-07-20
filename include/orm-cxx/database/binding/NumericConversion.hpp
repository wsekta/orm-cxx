#pragma once

#include <bit>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "ConversionError.hpp"

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
