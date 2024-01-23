#pragma once

#include "BindingPayload.hpp"
#include "orm-cxx/utils/DisableExternalsWarning.hpp"

DISABLE_WARNING_PUSH
DISABLE_EXTERNAL_WARNINGS
#include "soci/values.h"
DISABLE_WARNING_POP

namespace orm::db::binding
{
template <typename ModelAsTuple, typename T, std::size_t TupleSize = std::tuple_size_v<ModelAsTuple>>
auto getObjectFromValues(ModelAsTuple& modelAsTuple, const BindingPayload<T>& bindingPayload,
                         const soci::values& values) -> void
{
    getObjectFoldIteration(modelAsTuple, std::make_index_sequence<TupleSize>{}, bindingPayload, values);
}

template <typename T, typename ModelAsTuple, std::size_t... Is>
auto getObjectFoldIteration(ModelAsTuple& modelAsTuple, std::index_sequence<Is...>,
                            const BindingPayload<T>& bindingPayload, const soci::values& values) -> void
{
    (getObjectFoldIterationStep<Is>(modelAsTuple, bindingPayload, values), ...);
}

template <typename ModelField>
struct ObjectFieldFromValues
{
    static auto get(ModelField* column, const orm::model::ColumnInfo& columnInfo, const BindingInfo /*bindingInfo*/,
                    const soci::values& values) -> void
    {
        *column = values.get<ModelField>(columnInfo.name);
    }
};

template <typename ModelField>
struct ObjectFieldFromValues<std::optional<ModelField>>
{
    static auto get(std::optional<ModelField>* column, const orm::model::ColumnInfo& columnInfo,
                    const BindingInfo /*bindingInfo*/, const soci::values& values) -> void
    {
        if (values.get_indicator(columnInfo.name) == soci::i_null)
        {
            *column = std::nullopt;
        }
        else
        {
            *column = values.get<ModelField>(columnInfo.name);
        }
    }
};

template <>
struct ObjectFieldFromValues<float>
{
    static auto get(float* column, const orm::model::ColumnInfo& columnInfo, const BindingInfo /*bindingInfo*/,
                    const soci::values& values) -> void
    {
        *column = static_cast<float>(values.get<double>(columnInfo.name));
    }
};

template <>
struct ObjectFieldFromValues<std::optional<float>>
{
    static auto get(std::optional<float>* column, const orm::model::ColumnInfo& columnInfo,
                    const BindingInfo /*bindingInfo*/, const soci::values& values) -> void
    {
        if (values.get_indicator(columnInfo.name) == soci::i_null)
        {
            *column = std::nullopt;
        }
        else
        {
            *column = static_cast<float>(values.get<double>(columnInfo.name));
        }
    }
};

template <std::size_t Is, typename T, typename ModelAsTuple>
inline auto getObjectFoldIterationStep(ModelAsTuple& modelAsTuple, const BindingPayload<T>& bindingPayload,
                                       const soci::values& values) -> void
{
    using field_t = std::decay_t<std::invoke_result_t<decltype([](auto t) { return *std::get<Is>(t); }), ModelAsTuple>>;
    ObjectFieldFromValues<field_t>::get(std::get<Is>(modelAsTuple), bindingPayload.getModelInfo().columnsInfo.at(Is),
                                        bindingPayload.bindingInfo, values);
}
}
