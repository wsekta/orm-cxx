#pragma once

#include <optional>
#include <vector>

#include "orm-cxx/model/ModelInfo.hpp"
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

struct UpdateData
{
    const model::ModelInfo& modelInfo;
    std::vector<UpdateAssignment> assignments;
    std::optional<Predicate> predicate = std::nullopt;
};
} // namespace orm::query
