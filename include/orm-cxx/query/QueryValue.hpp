#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

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

    QueryValue(bool value) : value{static_cast<int>(value)} {}
    QueryValue(char value) : value{static_cast<int>(value)} {}
    QueryValue(signed char value) : value{static_cast<int>(value)} {}
    QueryValue(unsigned char value) : value{static_cast<int>(value)} {}
    QueryValue(short value) : value{static_cast<int>(value)} {}
    QueryValue(unsigned short value) : value{static_cast<int>(value)} {}
    QueryValue(int value) : value{value} {}
    QueryValue(unsigned int value) : value{static_cast<unsigned long long>(value)} {}
    QueryValue(long value) : value{static_cast<long long>(value)} {}
    QueryValue(unsigned long value) : value{static_cast<unsigned long long>(value)} {}
    QueryValue(long long value) : value{value} {}
    QueryValue(unsigned long long value) : value{value} {}
    QueryValue(float value) : value{static_cast<double>(value)} {}
    QueryValue(double value) : value{value} {}
    QueryValue(const char* value) : value{std::string{value}} {}
    QueryValue(std::string value) : value{std::move(value)} {}
    QueryValue(std::string_view value) : value{std::string{value}} {}

    template <std::size_t Size>
    QueryValue(const char (&value)[Size]) : value{std::string{value}}
    {
    }

    template <typename T>
    QueryValue(const std::optional<T>& optionalValue) : QueryValue(optionalValue.value())
    {
    }

    [[nodiscard]] auto get() const -> const Value&
    {
        return value;
    }

private:
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
