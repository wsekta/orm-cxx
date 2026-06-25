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

    QueryValue(bool inputValue) : value{static_cast<int>(inputValue)} {}
    QueryValue(char inputValue) : value{static_cast<int>(inputValue)} {}
    QueryValue(signed char inputValue) : value{static_cast<int>(inputValue)} {}
    QueryValue(unsigned char inputValue) : value{static_cast<int>(inputValue)} {}
    QueryValue(short inputValue) : value{static_cast<int>(inputValue)} {}
    QueryValue(unsigned short inputValue) : value{static_cast<int>(inputValue)} {}
    QueryValue(int inputValue) : value{inputValue} {}
    QueryValue(unsigned int inputValue) : value{static_cast<unsigned long long>(inputValue)} {}
    QueryValue(long inputValue) : value{static_cast<long long>(inputValue)} {}
    QueryValue(unsigned long inputValue) : value{static_cast<unsigned long long>(inputValue)} {}
    QueryValue(long long inputValue) : value{inputValue} {}
    QueryValue(unsigned long long inputValue) : value{inputValue} {}
    QueryValue(float inputValue) : value{static_cast<double>(inputValue)} {}
    QueryValue(double inputValue) : value{inputValue} {}
    QueryValue(const char* inputValue) : value{std::string{inputValue}} {}
    QueryValue(std::string inputValue) : value{std::move(inputValue)} {}
    QueryValue(std::string_view inputValue) : value{std::string{inputValue}} {}

    template <std::size_t Size>
    QueryValue(const char (&inputValue)[Size]) : value{std::string{inputValue}}
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
