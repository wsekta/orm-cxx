#pragma once

#include "model/GetModelInfo.hpp"
#include "relations.hpp"

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
    static auto getModelInfo(bool force = false) -> model::ModelInfo&
    {
        return model::getCachedModelInfo<T>(force);
    }
};
} // namespace orm
