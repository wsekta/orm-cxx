#pragma once

#include <charconv>
#include <cstddef>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>

#include "ConversionError.hpp"
#include "NumericConversion.hpp"
#include "soci/values.h"

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
