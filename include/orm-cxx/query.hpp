#pragma once

#include <optional>
#include <string>

#include "model.hpp"

namespace orm
{
/**
 * @brief A template class representing a query in the ORM framework.
 *
 * This class provides functionality for defining and accessing columns in a query.
 * It is a base class that can be inherited by specific query classes.
 *
 * @tparam T The type of the query.
 */
template <typename T>
class Query
{
public:
    
    /**
     * @brief Sets the OFFSET clause for the query.
     * @param offset The number of rows to skip.
     * @return A reference to the QueryBuilder object.
     */
    inline auto offset(std::size_t offset)
    {
        queryOffset = offset;

        return *this;
    }

    /**
     * @brief Sets the LIMIT clause for the query.
     * @param limit The maximum number of rows to return.
     * @return A reference to the QueryBuilder object.
     */
    inline auto limit(std::size_t limit)
    {
        queryLimit = limit;

        return *this;
    }

    /**
     * @brief Builds the SQL SELECT query string.
     * @return The constructed query string.
     */
    inline auto buildQuery() -> std::string const
    {
        using namespace std::string_literals;

        Model<T> model;
        std::string query = "SELECT * FROM "s.append(model.getTableName());

        if (queryOffset.has_value())
        {
            query.append(" OFFSET "s.append(std::to_string(queryOffset.value())));
        }

        if (queryLimit.has_value())
        {
            query.append(" LIMIT "s.append(std::to_string(queryLimit.value())));
        }

        return query.append(";");
    }

private:
    std::optional<std::size_t> queryOffset = std::nullopt; /**< The optional OFFSET value for the query. */
    std::optional<std::size_t> queryLimit = std::nullopt; /**< The optional LIMIT value for the query. */
};
}
