#pragma once

#include "BindingPayload.hpp"
#include "GetObjectFromValues.hpp"
#include "SetObjectToValues.hpp"
#include "orm-cxx/utils/DisableExternalsWarning.hpp"

DISABLE_WARNING_PUSH

DISABLE_EXTERNAL_WARNINGS

#include "rfl/to_view.hpp"
#include "soci/type-conversion.h"
#include "soci/values.h"

DISABLE_WARNING_POP

namespace soci
{
template <typename T>
using BindingPayload = orm::db::binding::BindingPayload<T>;

template <typename T>
struct type_conversion<BindingPayload<T>>
{
    using base_type = values;

    [[maybe_unused]] static void from_base(const values& v, indicator /*ind*/, BindingPayload<T>& model)
    {
        auto& modelValue = model.value;
        auto modelAsTuple = rfl::to_view(modelValue).values();

        orm::db::binding::getObjectFromValues(modelAsTuple, model, v);
    }

    [[maybe_unused]] static void to_base(const BindingPayload<T>& model, values& v, indicator& ind)
    {
        auto& modelValue = model.value;
        auto modelAsTuple = rfl::to_view(modelValue).values();

        orm::db::binding::setObjectToValues(modelAsTuple, model, v);

        ind = i_ok;
    }
};
} // namespace soci
