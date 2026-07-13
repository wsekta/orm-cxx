#pragma once

#include <cmath>
#include <compare>
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "orm-cxx/model/ColumnType.hpp"

namespace orm::query
{
/**
 * @brief A value that can be bound as a query parameter.
 *
 * QueryValue stores the subset of C++ values supported by the SELECT query DSL.
 * Values are rendered as SOCI bind parameters, not interpolated into SQL.
 */
class QueryValue
{
public:
    using Value = std::variant<int, long long, unsigned long long, double, std::string>;

    QueryValue(bool inputValue) : logicalType{model::ColumnType::Bool}, value{static_cast<int>(inputValue)} {}
    QueryValue(char inputValue) : logicalType{model::ColumnType::Char}, value{static_cast<int>(inputValue)} {}
    QueryValue(signed char inputValue) : logicalType{model::ColumnType::Char}, value{static_cast<int>(inputValue)} {}
    QueryValue(unsigned char inputValue)
        : logicalType{model::ColumnType::UnsignedChar}, value{static_cast<int>(inputValue)}
    {
    }
    QueryValue(short inputValue) : logicalType{model::ColumnType::Short}, value{static_cast<int>(inputValue)} {}
    QueryValue(unsigned short inputValue)
        : logicalType{model::ColumnType::UnsignedShort}, value{static_cast<int>(inputValue)}
    {
    }
    QueryValue(int inputValue) : logicalType{model::ColumnType::Int}, value{inputValue} {}
    QueryValue(unsigned int inputValue)
        : logicalType{model::ColumnType::UnsignedInt}, value{static_cast<unsigned long long>(inputValue)}
    {
    }
    QueryValue(long inputValue)
        : logicalType{sizeof(long) > sizeof(int) ? model::ColumnType::LongLong : model::ColumnType::Int},
          value{storeLong(inputValue)}
    {
    }
    QueryValue(unsigned long inputValue)
        : logicalType{sizeof(unsigned long) > sizeof(unsigned int) ? model::ColumnType::UnsignedLongLong :
                                                                     model::ColumnType::UnsignedInt},
          value{static_cast<unsigned long long>(inputValue)}
    {
    }
    QueryValue(long long inputValue) : logicalType{model::ColumnType::LongLong}, value{inputValue} {}
    QueryValue(unsigned long long inputValue) : logicalType{model::ColumnType::UnsignedLongLong}, value{inputValue} {}
    QueryValue(float inputValue) : logicalType{model::ColumnType::Float}, value{static_cast<double>(inputValue)} {}
    QueryValue(double inputValue) : logicalType{model::ColumnType::Double}, value{inputValue} {}
    QueryValue(const char* inputValue) : logicalType{model::ColumnType::String}, value{std::string{inputValue}} {}
    QueryValue(std::string inputValue) : logicalType{model::ColumnType::String}, value{std::move(inputValue)} {}
    QueryValue(std::string_view inputValue) : logicalType{model::ColumnType::String}, value{std::string{inputValue}} {}

    template <std::size_t Size>
    QueryValue(const char (&inputValue)[Size]) : logicalType{model::ColumnType::String}, value{std::string{inputValue}}
    {
    }

    template <typename T>
    QueryValue(const std::optional<T>& optionalValue) : QueryValue(optionalValue.value())
    {
    }

    /**
     * @brief Restores a canonical transport value with its model-level type.
     *
     * Hydration and backend adapters use this factory when several logical
     * column types share the same SOCI exchange representation.
     */
    [[nodiscard]] static auto fromStorage(model::ColumnType logicalType, Value value) -> QueryValue
    {
        if (not isCompatibleStorage(logicalType, value))
        {
            throw std::invalid_argument{"Query value storage does not match its logical column type"};
        }

        return QueryValue{logicalType, std::move(value), StorageValueTag{}};
    }

    /**
     * @brief Checks both the canonical variant alternative and its logical range.
     */
    [[nodiscard]] static auto isCompatibleStorage(model::ColumnType logicalType, const Value& storedValue) noexcept
        -> bool
    {
        const auto* intValue = std::get_if<int>(&storedValue);
        const auto* unsignedValue = std::get_if<unsigned long long>(&storedValue);
        const auto* doubleValue = std::get_if<double>(&storedValue);

        switch (logicalType)
        {
        case model::ColumnType::Bool:
            return intValue != nullptr and (*intValue == 0 or *intValue == 1);
        case model::ColumnType::Char:
            return intValue != nullptr and *intValue >= std::numeric_limits<signed char>::lowest() and
                   *intValue <= std::numeric_limits<unsigned char>::max();
        case model::ColumnType::UnsignedChar:
            return intValue != nullptr and *intValue >= 0 and *intValue <= std::numeric_limits<unsigned char>::max();
        case model::ColumnType::Short:
            return intValue != nullptr and *intValue >= std::numeric_limits<short>::lowest() and
                   *intValue <= std::numeric_limits<short>::max();
        case model::ColumnType::UnsignedShort:
            return intValue != nullptr and *intValue >= 0 and *intValue <= std::numeric_limits<unsigned short>::max();
        case model::ColumnType::Int:
            return intValue != nullptr;
        case model::ColumnType::UnsignedInt:
            return unsignedValue != nullptr and *unsignedValue <= std::numeric_limits<unsigned int>::max();
        case model::ColumnType::LongLong:
            return std::holds_alternative<long long>(storedValue);
        case model::ColumnType::UnsignedLongLong:
            return unsignedValue != nullptr;
        case model::ColumnType::Float:
            return doubleValue != nullptr and std::isfinite(*doubleValue) and
                   *doubleValue >= static_cast<double>(std::numeric_limits<float>::lowest()) and
                   *doubleValue <= static_cast<double>(std::numeric_limits<float>::max()) and
                   static_cast<double>(static_cast<float>(*doubleValue)) == *doubleValue;
        case model::ColumnType::Double:
            return doubleValue != nullptr and std::isfinite(*doubleValue);
        case model::ColumnType::String:
            return std::holds_alternative<std::string>(storedValue);
        case model::ColumnType::Uuid:
        case model::ColumnType::Unknown:
        case model::ColumnType::OneToOne:
            return false;
        }

        return false;
    }

    [[nodiscard]] auto getLogicalType() const noexcept -> model::ColumnType
    {
        return logicalType;
    }

    [[nodiscard]] auto get() const -> const Value&
    {
        return value;
    }

    auto operator<=>(const QueryValue&) const = default;

private:
    struct StorageValueTag
    {
    };

    [[nodiscard]] static auto storeLong(long inputValue) -> Value
    {
        if constexpr (sizeof(long) > sizeof(int))
        {
            return Value{static_cast<long long>(inputValue)};
        }
        else
        {
            return Value{static_cast<int>(inputValue)};
        }
    }

    QueryValue(model::ColumnType logicalTypeInit, Value valueInit, StorageValueTag)
        : logicalType{logicalTypeInit}, value{std::move(valueInit)}
    {
    }

    model::ColumnType logicalType;
    Value value;
};

/**
 * @brief A named parameter for raw query fragments.
 */
struct QueryParameter
{
    std::string name;
    QueryValue value;
};

/**
 * @brief Creates a named parameter for a raw SQL fragment.
 *
 * The name must not include the leading ':'.
 */
template <typename T>
auto param(std::string name, T value) -> QueryParameter
{
    return QueryParameter{.name = std::move(name), .value = QueryValue{std::move(value)}};
}
} // namespace orm::query
