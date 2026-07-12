#pragma once

#include "orm-cxx/model.hpp"

namespace orm::db::binding
{
template <typename T, bool JoinedValues = false>
struct BindingPayload
{
    mutable T value;
    inline static constexpr bool joinedValues = JoinedValues;

    auto getModelInfo() const -> const model::ModelInfo&
    {
        static const auto& modelInfo = Model<T>::getModelInfo();

        return modelInfo;
    }
};
} // namespace orm::db::binding
