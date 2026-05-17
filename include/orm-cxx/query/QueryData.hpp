#pragma once

#include <optional>
#include <string>
#include <vector>

#include "OrderBy.hpp"
#include "orm-cxx/model/ModelInfo.hpp"
#include "Predicate.hpp"

namespace orm::query
{
struct QueryData
{
    const model::ModelInfo& modelInfo;                 /**< The model info for the query. */
    std::optional<std::size_t> offset = std::nullopt;  /**< The optional OFFSET value for the query. */
    std::optional<std::size_t> limit = std::nullopt;   /**< The optional LIMIT value for the query. */
    std::optional<Predicate> predicate = std::nullopt; /**< The optional WHERE predicate tree. */
    std::vector<OrderBy> orderBy;                      /**< ORDER BY clauses. */
    bool isDistinct = false; /**< The flag that indicates if SELECT DISTINCT should be used. */
    bool shouldJoin = true;  /**< The flag that indicates if the query should join. */
};
} // namespace orm::query
