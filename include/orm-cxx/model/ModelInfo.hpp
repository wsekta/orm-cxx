#pragma once

#include "ColumnInfo.hpp"
#include "IdInfo.hpp"
#include "TableInfo.hpp"

namespace orm::model
{
struct ModelInfo
{
    std::string tableName;
    std::vector<ColumnInfo> columnsInfo;
    std::unordered_set<std::string> idColumnsNames;
    std::unordered_map<std::string, ModelInfo> foreignModelsInfo;
};

template <typename T>
auto generateModelInfo() -> ModelInfo
{
    ModelInfo modelInfo;

    modelInfo.idColumnsNames = getPrimaryIdColumnsNames<T>();
    modelInfo.tableName = getTableName<T>();
    modelInfo.columnsInfo = getColumnsInfo<T>(modelInfo.idColumnsNames);

    T model{};
    auto values = rfl::to_view(model).values();

    //    std::invoke_result_t<rfl::to_view, T> a;

    getForeignModelInfoFromModel<T>(values, modelInfo);

    return modelInfo;
}

template <typename T, typename ModelAsTuple, std::size_t TupleSize = std::tuple_size_v<ModelAsTuple>>
static auto getForeignModelInfoFromModel(ModelAsTuple& modelAsTuple, ModelInfo& modelInfo) -> void
{
    getForeignModelInfoFoldIteration<T>(modelAsTuple, std::make_index_sequence<TupleSize>{}, modelInfo);
}

template <typename T, typename ModelAsTuple, std::size_t... Is>
static auto getForeignModelInfoFoldIteration(ModelAsTuple& modelAsTuple, std::index_sequence<Is...>,
                                             ModelInfo& modelInfo) -> void
{
    (getForeignModelInfoFromField<T>(Is, std::get<Is>(modelAsTuple), modelInfo), ...);
}

template <typename T, typename ModelField>
static auto getForeignModelInfoFromField(std::size_t i, ModelField*, ModelInfo& modelInfo) -> void
{
    if constexpr (checkIfIsModelWithId<ModelField>())
    {
        auto name = rfl::fields<T>()[i].name();

        modelInfo.columnsInfo[i].isForeignModel = true;
        
        modelInfo.columnsInfo[i].type = ColumnType::OneToMany;

        modelInfo.foreignModelsInfo[name] = generateModelInfo<ModelField>();
    }
}

template <typename T, typename ModelField>
static auto getForeignModelInfoFromField(std::size_t i, std::optional<ModelField>*, ModelInfo& modelInfo) -> void
{
    getForeignModelInfoFromField<T>(i, static_cast<ModelField*>(nullptr), modelInfo);
}
}
