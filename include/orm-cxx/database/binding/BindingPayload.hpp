#pragma once

#include "BindingInfo.hpp"
#include "orm-cxx/model.hpp"

namespace orm::db::binding
{
template <typename T>
struct BindingPayload
{
    mutable T value;

    [[maybe_unused]] inline static BindingInfo bindingInfo = {};

    auto getModelInfo() const -> const model::ModelInfo&
    {
        static const auto& modelInfo = Model<T>::getModelInfo();

        return modelInfo;
    }
};
} // namespace orm::db::binding
