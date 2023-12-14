#pragma once

#include <array>
#include <boost/pfr/core_name.hpp>
#include <string_view>

#include "name.hpp"

namespace orm::model
{
/**
 * @brief Get the number of columns in a model.
 *
 * @tparam T The type of the model.
 * @return The number of columns in the model.
 */
template <typename T>
constexpr static auto columnCount() -> std::size_t
{
    if constexpr (boost::pfr::tuple_size_v<T> == 0)
    {
        return 0;
    }
    else
    {
        return boost::pfr::tuple_size_v<T>;
    }
}

/**
 * @brief Set the column names for a model.
 *
 * @tparam T The type of the model.
 * @return An array of column names.
 */
template <typename T>
constexpr static auto setColumnNames() -> std::array<std::string_view, columnCount<T>()>
{
    std::array<std::string_view, columnCount<T>()> columnNames{};

    if constexpr (columnCount<T>() == 0)
    {
        return columnNames;
    }
    else
    {
        return boost::pfr::names_as_array<T>();
    }
}
}