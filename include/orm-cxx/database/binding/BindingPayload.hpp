#pragma once

#include "orm-cxx/model.hpp"

namespace orm::db::binding
{
template <typename T>
struct BindingPayload
{
    mutable T value;

    auto getModelInfo() const -> const model::ModelInfo&
    {
        static const auto& modelInfo = Model<T>::getModelInfo();

        return modelInfo;
    }
};
}
