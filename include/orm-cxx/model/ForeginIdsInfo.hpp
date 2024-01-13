#pragma once

#include <optional>
#include <rfl.hpp>
#include <string>
#include <unordered_map>

#include "ColumnInfo.hpp"
#include "ForeginIdsInfoType.hpp"
#include "IdInfo.hpp"
#include "TableInfo.hpp"

namespace orm::model
{
template <typename T>
auto getForeignIdsInfo() -> ForeignIdsInfo
{
    ForeignIdsInfo foreignIdsInfo{};

    T model{};
    auto values = rfl::to_view(model).values();

    getForeignIdsFromModel<T>(values, foreignIdsInfo);

    return foreignIdsInfo;
}

// getting model from values
template <typename T, typename ModelAsTuple, std::size_t TupleSize = std::tuple_size_v<ModelAsTuple>>
static auto getForeignIdsFromModel(ModelAsTuple& modelAsTuple, ForeignIdsInfo& foreignIdsInfo) -> void
{
    getForeignIdsFoldIteration<T>(modelAsTuple, std::make_index_sequence<TupleSize>{}, foreignIdsInfo);
}

template <typename T, typename ModelAsTuple, std::size_t... Is>
static auto getForeignIdsFoldIteration(ModelAsTuple& modelAsTuple, std::index_sequence<Is...>,
                                       ForeignIdsInfo& foreignIdsInfo) -> void
{
    (getForeignIdsFromField<T>(Is, std::get<Is>(modelAsTuple), foreignIdsInfo), ...);
}

template <typename T, typename ModelField>
static auto getForeignIdsFromField(std::size_t i, ModelField* /*field*/, ForeignIdsInfo& foreignIdsInfo) -> void
{
    if constexpr (checkIfIsModelWithId<ModelField>())
    {
        auto name = rfl::fields<T>()[i].name();

        auto fieldIdsInfo = getPrimaryIdColumnsNames<ModelField>();

        auto fieldColumnsInfo = getColumnsInfo<ModelField>(fieldIdsInfo, {});

        for (const auto& columnInfo : fieldColumnsInfo)
        {
            if (columnInfo.isPrimaryKey)
            {
                foreignIdsInfo[name].tableName = getTableName<ModelField>();
                foreignIdsInfo[name].idFields[columnInfo.name] = columnInfo;
            }
        }
    }
}

template <typename T, typename ModelField>
static auto getForeignIdsFromField(std::size_t i, std::optional<ModelField>* /*field*/, ForeignIdsInfo& foreignIdsInfo)
    -> void
{
    ModelField modelField{};
    getForeignIdsFromField<T>(i, &modelField, foreignIdsInfo);
}
}