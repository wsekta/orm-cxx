#pragma once

#include <array>
#include <boost/pfr/core_name.hpp>
#include <string_view>

#include "model/name.hpp"
#include "model/columns.hpp"

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
    inline constexpr static std::array<std::string_view, model::columnCount<T>()> columnNames = model::setColumnNames<T>();

    inline constexpr static std::string_view tableName = model::tableName<T>();

public:
    /**
     * @brief Get the column names for the model.
     * 
     * @return A reference to an array of column names.
     */
    const static std::array<std::string_view, model::columnCount<T>()>& getColumnNames()
    {
        return columnNames;
    }
    
    /**
     * @brief Get the table name for the model.
     * 
     * @return The table name as a string view.
     */
    static std::string_view getTableName()
    {
        return tableName;
    }
};
}
