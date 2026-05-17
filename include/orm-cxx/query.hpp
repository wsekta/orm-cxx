#pragma once

#include <cstddef>
#include <optional>
#include <utility>

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
     * @brief Replaces the WHERE predicate.
     * @param predicate
     */
    auto where(const query::Predicate& predicate) -> Query<T>&
    {
        data.predicate = predicate;

        return *this;
    }

    /**
     * @brief Adds a predicate with AND.
     * @param predicate
     */
    auto andWhere(const query::Predicate& predicate) -> Query<T>&
    {
        data.predicate = data.predicate.has_value() ? data.predicate.value() && predicate : predicate;

        return *this;
    }

    /**
     * @brief Adds a predicate with OR.
     * @param predicate
     */
    auto orWhere(const query::Predicate& predicate) -> Query<T>&
    {
        data.predicate = data.predicate.has_value() ? data.predicate.value() || predicate : predicate;

        return *this;
    }

    /**
     * @brief Sets the ORDER BY clauses for the select query.
     * @param orders The ORDER BY clauses.
     *
     * @return A reference to the QueryBuilder object.
     */
    template <typename... Orders>
    auto orderBy(Orders... orders) -> Query<T>&
    {
        data.orderBy = {std::move(orders)...};

        return *this;
    }

    /**
     * @brief Select distinct rows.
     * @return A reference to the QueryBuilder object.
     */
    inline auto distinct() -> Query<T>&
    {
        data.isDistinct = true;

        return *this;
    }

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

    /**
     * @brief Disables joining for the select query.
     * @note Only ids fields will be set in related models.
     * @return A reference to the QueryBuilder object.
     */
    inline auto disableJoining() -> Query<T>&
    {
        data.shouldJoin = false;

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
    [[nodiscard]] inline auto getData() const -> const query::QueryData&
    {
        return data;
    }

    query::QueryData data; /**< The query data. */
};
} // namespace orm
