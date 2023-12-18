#pragma once

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
public:
    /**
     * @brief Get the column names for the model.
     * 
     * @return A reference to an array of column names.
     */
    static std::vector<std::string> getColumnNames()
    {
        return model::setColumnNames<T>();
    }
    
    /**
     * @brief Get the table name for the model.
     * 
     * @return The table name as a string view.
     */
    static std::string getTableName()
    {
        return model::tableName<T>();
    }
};
}
