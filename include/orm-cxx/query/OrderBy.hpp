#pragma once

#include <string>
#include <utility>

#include "Predicate.hpp"

namespace orm::query
{
enum class OrderDirection
{
    Asc,
    Desc,
};

/**
 * @brief A single ORDER BY clause.
 */
struct OrderBy
{
    Column column{""};
    OrderDirection direction{OrderDirection::Asc};
    std::string rawSql{};
    bool isRaw{false};
};

/**
 * @brief Creates an ascending ORDER BY clause.
 */
inline auto asc(Column column) -> OrderBy
{
    return OrderBy{.column = std::move(column), .direction = OrderDirection::Asc};
}

/**
 * @brief Creates a descending ORDER BY clause.
 */
inline auto desc(Column column) -> OrderBy
{
    return OrderBy{.column = std::move(column), .direction = OrderDirection::Desc};
}

/**
 * @brief Creates a raw ORDER BY clause.
 */
inline auto rawOrder(std::string sql) -> OrderBy
{
    return OrderBy{.rawSql = std::move(sql), .isRaw = true};
}
} // namespace orm::query
