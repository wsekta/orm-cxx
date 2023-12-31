#pragma once

#include "model/ModelInfo.hpp"

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
     * @brief Get the model info for the model.
     *
     * @return A reference to the model info.
     */
    static model::ModelInfo& getModelInfo()
    {
        if (not modelInfo.has_value())
        {
            modelInfo = model::generateModelInfo<T>();
        }

        return modelInfo.value();
    }

    /**
     * @brief Get the temporary object of the model.
     *
     * @return Reference to the temporary object.
     */
    inline T& getObject() const
    {
        return object;
    }

private:
    mutable T object;

    inline static std::optional<model::ModelInfo> modelInfo = std::nullopt;
};
}
