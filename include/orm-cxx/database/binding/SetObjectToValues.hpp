#include "orm-cxx/utils/DisableExternalsWarning.hpp"
#include "BindingPayload.hpp"
#include "ObjectFieldToValues.hpp"

DISABLE_WARNING_PUSH
DISABLE_EXTERNAL_WARNINGS
#include "soci/values.h"
DISABLE_WARNING_POP

namespace orm::db::binding
{
template <typename ModelAsTuple, typename T, std::size_t TupleSize = std::tuple_size_v<ModelAsTuple>>
auto setObjectToValues(const ModelAsTuple& modelAsTuple, const BindingPayload<T>& bindingPayload,
                       soci::values& values) -> void
{
    setObjectFoldIteration(modelAsTuple, std::make_index_sequence<TupleSize>{}, bindingPayload, values);
}

template <typename T, typename ModelAsTuple, std::size_t... Is>
auto setObjectFoldIteration(const ModelAsTuple& modelAsTuple, std::index_sequence<Is...>,
                            const BindingPayload<T>& bindingPayload, soci::values& values) -> void
{
    (setObjectFoldIterationStep<Is>(modelAsTuple, bindingPayload, values), ...);
}

template <std::size_t Is, typename T, typename ModelAsTuple>
auto setObjectFoldIterationStep(const ModelAsTuple& modelAsTuple, const BindingPayload<T>& bindingPayload,
                                soci::values& values) -> void
{
    using field_t = std::decay_t<std::invoke_result_t<decltype([](auto t) { return *std::get<Is>(t); }), ModelAsTuple>>;
    ObjectFieldToValues<field_t>::set(std::get<Is>(modelAsTuple), 
                                      bindingPayload.getModelInfo().columnsInfo.at(Is),
                                      bindingPayload.bindingInfo,
                                      values);
}
} // namespace orm::db::binding
