#pragma once

#include <optional>
#include <string>

#include "orm-cxx/model/ModelInfo.hpp"

namespace orm::query
{
struct QueryData
{
    std::optional<std::size_t> offset = std::nullopt; /**< The optional OFFSET value for the query. */
    std::optional<std::size_t> limit = std::nullopt;  /**< The optional LIMIT value for the query. */
    const model::ModelInfo& modelInfo;                /**< The model info for the query. */
};
}