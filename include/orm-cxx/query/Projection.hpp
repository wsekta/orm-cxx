#pragma once

#include <string>
#include <utility>

#include "Predicate.hpp"

namespace orm::query
{
/**
 * @brief A projected source column and the DTO field alias it hydrates.
 */
struct Projection
{
    std::string resultField;
    Column sourceColumn{""};
};

/**
 * @brief Projects a source model column path into a result DTO field.
 */
inline auto as(std::string resultField, Column sourceColumn) -> Projection
{
    return Projection{.resultField = std::move(resultField), .sourceColumn = std::move(sourceColumn)};
}
} // namespace orm::query
