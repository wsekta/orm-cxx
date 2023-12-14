#pragma once

#include <array>
#include <boost/pfr/core_name.hpp>
#include <string_view>

namespace orm
{
/**
 * @brief A template class representing a model in the ORM framework.
 *
 * This class provides functionality for defining and accessing columns in a model.
 * It is a base class that can be inherited by specific model classes.
 *
 * @tparam T The type of the model.
 */
template <typename T>
class Model
{
private:
    /**
     * @brief Returns the number of columns in the model.
     *
     * This function is a consteval function, which means it is evaluated at compile-time.
     *
     * @return The number of columns in the model.
     */
    consteval static std::size_t columnCount()
    {
        std::size_t count = 0;

        count = boost::pfr::tuple_size_v<T>;

        return count;
    }

    constexpr static auto setColumnNames() -> std::array<std::string_view, columnCount()>
    {
        if constexpr (columnCount() == 0)
        {
            return std::array<std::string_view, 0>{};
        }
        else
        {
            auto names = boost::pfr::names_as_array<T>();

            return names;
        }
    }

    /**
     * @brief An array of column names in the model.
     *
     * This array is populated with column names at compile-time.
     */
    inline static std::array<std::string_view, columnCount()> columnNames = setColumnNames();

public:
    /**
     * @brief Returns a reference to the array of column names in the model.
     *
     * @return A reference to the array of column names.
     */
    const static std::array<std::string_view, columnCount()>& getColumnNames()
    {
        return columnNames;
    }
};
}
