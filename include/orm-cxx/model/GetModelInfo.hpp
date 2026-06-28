#pragma once

#include <type_traits>
#include <utility>

#include "AutoIncrementInfo.hpp"
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
    const auto autoIncrementColumnsNames = getAutoIncrementColumnsNames<T>();
    modelInfo.tableName = getTableName<T>();
    modelInfo.columnsInfo = getColumnsInfo<T>(modelInfo.idColumnsNames);

    using model_tuple_t = std::decay_t<decltype(rfl::to_view(std::declval<T&>()).values())>;

    auto getForeignModelInfoFromField = [&modelInfo](auto i, auto field)
    { GetForeignModelInfoFromField<T, std::decay_t<decltype(*field)>>::get(i, modelInfo); };

    utils::constexpr_for_tuple<model_tuple_t>(getForeignModelInfoFromField);

    applyAutoIncrementColumns(modelInfo, autoIncrementColumnsNames);

    return modelInfo;
}
} // namespace orm::model
