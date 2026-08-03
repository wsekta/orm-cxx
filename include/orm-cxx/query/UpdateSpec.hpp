#pragma once

#include <optional>
#include <vector>

#include "Predicate.hpp"
#include "QueryValue.hpp"

namespace orm::query
{
struct UpdateValue
{
    std::optional<QueryValue> value;
};

struct UpdateAssignment
{
    Column column;
    UpdateValue value;
};

/**
 * @brief Runtime UPDATE options independent from static model metadata.
 */
struct UpdateSpec
{
    std::vector<UpdateAssignment> assignments;
    std::optional<Predicate> predicate = std::nullopt;
};
} // namespace orm::query
