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

    using model_tuple_t =
        std::decay_t<std::invoke_result_t<decltype([](auto t) { return rfl::to_view(t).values(); }), T>>;
    getForeignModelInfoFromModel<T, model_tuple_t>(modelInfo);

    return modelInfo;
}

template <typename T, typename ModelAsTuple, std::size_t TupleSize = std::tuple_size_v<ModelAsTuple>>
inline auto getForeignModelInfoFromModel(ModelInfo& modelInfo) -> void
{
    getForeignModelInfoFoldIteration<T, ModelAsTuple>(std::make_index_sequence<TupleSize>{}, modelInfo);
}

template <typename T, typename ModelAsTuple, std::size_t... Is>
inline auto getForeignModelInfoFoldIteration(std::index_sequence<Is...>, ModelInfo& modelInfo) -> void
{
    (getForeignModelInfoFoldIterationStep<T, Is, ModelAsTuple>(modelInfo), ...);
}

template <typename T, typename ModelField>
struct GetForeignModelInfoFromField
{
    static inline auto get(std::size_t i, ModelInfo& modelInfo) -> void
    {
        if constexpr (checkIfIsModelWithId<std::decay_t<ModelField>>())
        {
            auto name = rfl::fields<T>()[i].name();

            modelInfo.columnsInfo[i].isForeignModel = true;

            modelInfo.columnsInfo[i].type = ColumnType::OneToMany;

            modelInfo.foreignModelsInfo[name] = generateModelInfo<ModelField>();
        }
    }
};

template <typename T, typename ModelField>
struct GetForeignModelInfoFromField<T, std::optional<ModelField>>
{
    static inline auto get(std::size_t i, ModelInfo& modelInfo) -> void
    {
        GetForeignModelInfoFromField<T, ModelField>::get(i, modelInfo);
    }
};

template <typename T, std::size_t Is, typename ModelAsTuple>
inline auto getForeignModelInfoFoldIterationStep(ModelInfo& modelInfo) -> void
{
    using field_t = std::decay_t<std::invoke_result_t<decltype([](auto t) { return *std::get<Is>(t); }), ModelAsTuple>>;
    GetForeignModelInfoFromField<T, field_t>::get(Is, modelInfo);
}
} // namespace orm::model
