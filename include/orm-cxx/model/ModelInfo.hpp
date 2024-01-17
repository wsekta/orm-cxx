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

    using model_tuple_t = std::decay_t<
        std::invoke_result_t<
            decltype([](auto t){return rfl::to_view(t).values();}), 
            T>>;
    getForeignModelInfoFromModel<T>(static_cast<model_tuple_t*>(nullptr), modelInfo);

    return modelInfo;
}

template <typename T, typename ModelAsTuple, std::size_t TupleSize = std::tuple_size_v<ModelAsTuple>>
static auto getForeignModelInfoFromModel(ModelAsTuple* modelAsTuple, ModelInfo& modelInfo) -> void
{
    getForeignModelInfoFoldIteration<T>(modelAsTuple, std::make_index_sequence<TupleSize>{}, modelInfo);
}

template <typename T, typename ModelAsTuple, std::size_t... Is>
static auto getForeignModelInfoFoldIteration(ModelAsTuple* modelAsTuple, std::index_sequence<Is...>,
                                             ModelInfo& modelInfo) -> void
{
    (getForeignModelInfoFoldIterationStep<T, Is>(modelAsTuple, modelInfo), ...);
}

template <typename T, std::size_t Is, typename ModelAsTuple>
static auto getForeignModelInfoFoldIterationStep(ModelAsTuple*,
                                             ModelInfo& modelInfo) -> void
{
    using field_t = std::decay_t<
        std::invoke_result_t<
            decltype([](auto t){return *std::get<Is>(t);}), 
            ModelAsTuple>>;
    getForeignModelInfoFromField<T>(Is, static_cast<field_t*>(nullptr), modelInfo);
}

template <typename T, typename ModelField>
static auto getForeignModelInfoFromField(std::size_t i, ModelField*, ModelInfo& modelInfo) -> void
{
    if constexpr (checkIfIsModelWithId<std::decay_t<ModelField>>())
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
