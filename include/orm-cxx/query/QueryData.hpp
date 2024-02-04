#pragma once

#include <optional>
#include <string>

#include "Condition.hpp"
#include "orm-cxx/model/ModelInfo.hpp"

namespace orm::query
{
struct QueryData
{
    const model::ModelInfo& modelInfo;                 /**< The model info for the query. */
    std::optional<std::size_t> offset = std::nullopt;  /**< The optional OFFSET value for the query. */
    std::optional<std::size_t> limit = std::nullopt;   /**< The optional LIMIT value for the query. */
    std::optional<Condition> condition = std::nullopt; /**< The optional WHERE condition(s) */
    bool shouldJoin = true;                            /**< The flag that indicates if the query should join. */
};
}
