#pragma once

#include "GetForeignModelInfoFromField.hpp"
#include "ModelInfo.hpp"
#include "orm-cxx/utils/ConstexprFor.hpp"

namespace orm::model
{
template <typename T>
auto getModelInfo() -> ModelInfo
{
    ModelInfo modelInfo;

    modelInfo.idColumnsNames = getPrimaryIdColumnsNames<T>();
    modelInfo.tableName = getTableName<T>();
    modelInfo.columnsInfo = getColumnsInfo<T>(modelInfo.idColumnsNames);

    using model_tuple_t =
        std::decay_t<std::invoke_result_t<decltype([](auto t) { return rfl::to_view(t).values(); }), T>>;

    auto getForeignModelInfoFromField = [&modelInfo](auto i, auto field)
    { GetForeignModelInfoFromField<T, std::decay_t<decltype(*field)>>::get(i, modelInfo); };

    utils::constexpr_for_tuple<model_tuple_t>(getForeignModelInfoFromField);

    return modelInfo;
}
} // namespace orm::model
