module;

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

export module orm:sql_program;

import :foundation;
import :model;
import :expressions;

namespace orm::db::detail
{
[[nodiscard]] constexpr auto isPortableBindName(std::string_view name) noexcept -> bool
{
    if (name.empty())
    {
        return false;
    }

    for (const auto character : name)
    {
        const auto asciiLetter = (character >= 'a' and character <= 'z') or (character >= 'A' and character <= 'Z');
        const auto asciiDigit = character >= '0' and character <= '9';

        if (not asciiLetter and not asciiDigit and character != '_')
        {
            return false;
        }
    }

    return true;
}
} // namespace orm::db::detail
namespace orm::db
{
namespace detail
{
inline constexpr auto noSqlNode = std::numeric_limits<std::size_t>::max();
enum class SqlOperation
{
    Select,
    Update,
    Remove
};
enum class SqlNodeKind
{
    Comparison,
    Null,
    List,
    Between,
    And,
    Or,
    Not,
    Collection,
    Raw,
    Invalid
};

struct SqlSource
{
    std::array<std::string_view, 2> pathParts{};
    std::size_t pathSize{};
    bool isAggregate{};
    query::detail::AggregateFunction function{};
};

struct SqlNode
{
    SqlNodeKind kind{};
    SqlSource source{};
    unsigned operation{};
    std::size_t left = noSqlNode;
    std::size_t right = noSqlNode;
    std::size_t firstParameter{};
    std::size_t parameterCount{};
    std::string_view relation{};
    std::string_view rawSql{};
};

struct SqlOrder
{
    SqlSource source{};
    query::detail::OrderDirection direction{};
    bool isRaw{};
    std::string_view rawSql{};
};
struct SqlProjection
{
    SqlSource source{};
    std::string_view alias{};
};
struct SqlAssignment
{
    SqlSource source{};
    std::size_t parameter{};
};
struct SqlBindingDescriptor
{
    model::ColumnType logicalType{};
    std::size_t index{};
    std::string_view rawName{};
};

struct SqlQueryView
{
    SqlOperation operation{};
    std::span<const SqlNode> nodes{};
    std::size_t predicate = noSqlNode;
    std::size_t having = noSqlNode;
    std::span<const SqlOrder> orders{};
    std::span<const SqlSource> groups{};
    std::span<const SqlProjection> projections{};
    std::span<const SqlAssignment> assignments{};
    std::span<const std::string_view> includes{};
    std::span<const SqlBindingDescriptor> bindings{};
    bool isDistinct{};
    bool shouldJoin = true;
    bool hasLimit{};
    bool hasOffset{};
    bool boundPagination{};
    std::size_t limitParameter{};
    std::size_t offsetParameter{};
    std::size_t literalLimit{};
    std::size_t literalOffset{};
};

// Temporary storage is also valid during C++20 constant evaluation. The
// compiled statement copies it into exactly sized arrays before returning.
struct SqlProgram
{
    SqlOperation operation{};
    std::vector<SqlNode> nodes;
    std::size_t predicate = noSqlNode;
    std::size_t having = noSqlNode;
    std::vector<SqlOrder> orders;
    std::vector<SqlSource> groups;
    std::vector<SqlProjection> projections;
    std::vector<SqlAssignment> assignments;
    std::vector<std::string_view> includes;
    std::vector<SqlBindingDescriptor> bindings;
    bool isDistinct{};
    bool shouldJoin = true;
    bool hasLimit{};
    bool hasOffset{};
    bool boundPagination{};
    std::size_t limitParameter{};
    std::size_t offsetParameter{};
    std::size_t literalLimit{};
    std::size_t literalOffset{};

    [[nodiscard]] constexpr auto view() const -> SqlQueryView
    {
        return {operation,       nodes,          predicate,       having,       orders,       groups,   projections,
                assignments,     includes,       bindings,        isDistinct,   shouldJoin,   hasLimit, hasOffset,
                boundPagination, limitParameter, offsetParameter, literalLimit, literalOffset};
    }
};

[[nodiscard]] constexpr auto decimal(std::size_t value) -> std::string
{
    std::string result;
    do
    {
        result.push_back(static_cast<char>('0' + value % 10));
        value /= 10;
    } while (value != 0);
    std::ranges::reverse(result);
    return result;
}
template <typename... Parts>
[[nodiscard]] constexpr auto sqlConcat(const Parts&... parts) -> std::string
{
    std::string result;
    (result.append(parts), ...);
    return result;
}
[[nodiscard]] constexpr auto parameterName(const SqlBindingDescriptor& descriptor) -> std::string
{
    return descriptor.rawName.empty() ? sqlConcat("orm_p", decimal(descriptor.index)) : std::string{descriptor.rawName};
}
[[nodiscard]] constexpr auto quoteStandardIdentifier(std::string_view identifier,
                                                     CompiledSqlFlavor flavor) -> std::string
{
    if (identifier.empty())
        throw std::invalid_argument{"SQL identifier cannot be empty"};
    if (identifier.find('\0') != std::string_view::npos)
        throw std::invalid_argument{flavor == CompiledSqlFlavor::PostgreSQL ?
                                        "PostgreSQL identifiers must not contain an embedded NUL byte" :
                                        "SQLite identifiers must not contain an embedded NUL byte"};
    if (flavor == CompiledSqlFlavor::PostgreSQL && identifier.size() > 63)
        throw std::invalid_argument{"PostgreSQL identifiers must not exceed 63 bytes"};
    std::string result{"\""};
    for (const auto character : identifier)
    {
        if (character == '"')
            result += '"';
        result += character;
    }
    result += '"';
    return result;
}
} // namespace detail
} // namespace orm::db
