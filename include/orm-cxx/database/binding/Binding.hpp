#pragma once

#include "BindingPayload.hpp"
#include "ObjectFieldFromValues.hpp"
#include "ObjectFieldToValues.hpp"
#include "orm-cxx/utils//ConstexprFor.hpp"
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

    [[maybe_unused]] static void from_base(const soci::values& values, indicator /*ind*/, BindingPayload<T>& model)
    {
        auto& modelValue = model.value;
        auto modelAsTuple = rfl::to_view(modelValue).values();

        auto getObjectFromValues = [&model, &values](auto index, auto field)
        {
            using field_t = std::decay_t<decltype(*field)>;
            orm::db::binding::ObjectFieldFromValues<field_t>::get(field, model, index, values);
        };

        orm::utils::constexpr_for_tuple(modelAsTuple, getObjectFromValues);
    }

    [[maybe_unused]] static void to_base(const BindingPayload<T>& model, soci::values& values, indicator& ind)
    {
        auto& modelValue = model.value;
        auto modelAsTuple = rfl::to_view(modelValue).values();

        auto setObjectToValues = [&model, &values](auto index, auto field)
        {
            using field_t = std::decay_t<decltype(*field)>;
            orm::db::binding::ObjectFieldToValues<field_t>::set(field, model, index, values);
        };

        orm::utils::constexpr_for_tuple(modelAsTuple, setObjectToValues);

        ind = i_ok;
    }
};
} // namespace soci
