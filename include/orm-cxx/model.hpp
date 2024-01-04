#pragma once

#include "model/ColumnInfo.hpp"
#include "model/TableInfo.hpp"

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
     * @brief Get the table name for the model.
     *
     * @return The table name as a string view.
     */
    static std::string getTableName()
    {
        return model::getTableName<T>();
    }

    /**
     * @brief Get the columns info for the model.
     *
     * @return A reference to an array of column info.
     */
    static std::vector<model::ColumnInfo> getColumnsInfo()
    {
        return model::getColumnsInfo<T>();
    }

    /**
     * @brief Get the temporary object of the model.
     *
     * @return Reference to the temporary object.
     */
    inline T& getObject()
    {
        return object;
    }

private:
    T object;
};
}
