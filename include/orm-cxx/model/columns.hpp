#pragma once

#include <rfl.hpp>

#include "name.hpp"

namespace orm::model
{
/**
 * @brief Set the column names for a model.
 *
 * @tparam T The type of the model.
 * @return An array of column names.
 */
template <typename T>
auto setColumnNames() -> std::vector<std::string>
{
    auto fields = rfl::fields<T>();

    std::vector<std::string> columnNames;

    for (const auto& field : fields)
    {
        columnNames.push_back(field.name());
    }

    return columnNames;
}
}
