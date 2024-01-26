#pragma once

#include "BindingPayload.hpp"
#include "ObjectFieldFromValues.hpp"

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

template <std::size_t Is, typename T, typename ModelAsTuple>
auto getObjectFoldIterationStep(ModelAsTuple& modelAsTuple, const BindingPayload<T>& bindingPayload,
                                const soci::values& values) -> void
{
    using field_t = std::decay_t<std::invoke_result_t<decltype([](auto t) { return *std::get<Is>(t); }), ModelAsTuple>>;
    ObjectFieldFromValues<field_t>::get(std::get<Is>(modelAsTuple), bindingPayload.getModelInfo().columnsInfo.at(Is),
                                        bindingPayload.bindingInfo, values);
}
} // namespace orm::db::binding
