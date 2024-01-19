#pragma once

#include <optional>
#include <string>

#include "model.hpp"
#include "query/QueryData.hpp"

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
     * @brief Constructs a Query object.
     */
    Query() : data{.modelInfo = Model<T>().getModelInfo()} {}

    /**
     * @brief Sets the OFFSET clause for the query.
     * @param offset The number of rows to skip.
     * @return A reference to the QueryBuilder object.
     */
    inline auto offset(std::size_t offset)
    {
        data.offset = offset;

        return *this;
    }

    /**
     * @brief Sets the LIMIT clause for the query.
     * @param limit The maximum number of rows to return.
     * @return A reference to the QueryBuilder object.
     */
    inline auto limit(std::size_t limit)
    {
        data.limit = limit;

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

        auto modelInfo = model.getModelInfo();

        std::string query = "SELECT * FROM ";

        query.append(modelInfo.tableName);

        if (data.offset.has_value())
        {
            query.append(" OFFSET "s.append(std::to_string(data.offset.value())));
        }

        if (data.limit.has_value())
        {
            query.append(" LIMIT "s.append(std::to_string(data.limit.value())));
        }

        return query.append(";");
    }

    template <typename U>
    auto operator==(const Query<U>& rhs) -> bool
    {
        return data == rhs.getData();
    }

private:
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
