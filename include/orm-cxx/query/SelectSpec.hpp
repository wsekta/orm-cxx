#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Aggregate.hpp"
#include "OrderBy.hpp"
#include "Predicate.hpp"
#include "Projection.hpp"

namespace orm::query
{
/**
 * @brief Runtime SELECT options independent from static model metadata.
 */
struct SelectSpec
{
    std::optional<std::size_t> offset = std::nullopt;
    std::optional<std::size_t> limit = std::nullopt;
    std::optional<Predicate> predicate = std::nullopt;
    std::vector<OrderBy> orderBy;
    std::vector<Projection> projections;
    std::vector<Column> groupBy;
    std::optional<AggregatePredicate> having = std::nullopt;
    std::vector<std::string> includes;
    bool isDistinct = false;
    bool shouldJoin = true;
};
} // namespace orm::query
