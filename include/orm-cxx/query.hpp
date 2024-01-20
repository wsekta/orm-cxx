#pragma once

#include <optional>
#include <string>

#include "model.hpp"
#include "query/QueryData.hpp"

namespace orm
{
class Database;
/**
 * @brief A template class representing a select query in the ORM framework.
 *
 * This class provides functionality for building a select query.
 *
 * @tparam T The type of the query.
 */
template <typename T>
class Query
{
public:
    /**
     * @brief Constructs a Query object.
     */
    Query() : data{.modelInfo = Model<T>().getModelInfo()} {}

    /**
     * @brief Sets the OFFSET clause for the select query.
     * @param offset The number of rows to skip.
     * @return A reference to the QueryBuilder object.
     */
    inline auto offset(std::size_t offset) -> Query<T>&
    {
        data.offset = offset;

        return *this;
    }

    /**
     * @brief Sets the LIMIT clause for the select query.
     * @param limit The maximum number of rows to return.
     * @return A reference to the QueryBuilder object.
     */
    inline auto limit(std::size_t limit) -> Query<T>&
    {
        data.limit = limit;

        return *this;
    }

private:
    /**
     * @brief Database class is a friend class of Query for access to the query data.
     */
    friend class orm::Database;

    /**
     * @brief Gets the query data.
     * @return The query data.
     */
    inline auto getData() const -> const query::QueryData&
    {
        return data;
    }

    query::QueryData data; /**< The query data. */
};
}
