#pragma once

#include <string>
#include <utility>
#include <variant>

#include "Aggregate.hpp"

namespace orm::query
{
using ProjectionSource = std::variant<Column, AggregateExpression>;

/**
 * @brief A projected source expression and the DTO field alias it hydrates.
 */
struct Projection
{
    std::string resultField;
    ProjectionSource source;
};

/**
 * @brief Projects a source model column path into a result DTO field.
 */
inline auto as(std::string resultField, Column sourceColumn) -> Projection
{
    return Projection{.resultField = std::move(resultField), .source = std::move(sourceColumn)};
}

/**
 * @brief Projects an aggregate expression into a result DTO field.
 */
inline auto as(std::string resultField, AggregateExpression aggregate) -> Projection
{
    return Projection{.resultField = std::move(resultField), .source = std::move(aggregate)};
}

} // namespace orm::query
