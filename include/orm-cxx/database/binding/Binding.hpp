#pragma once

#include "BindingPayload.hpp"
#include "ObjectFieldFromValues.hpp"
#include "ObjectFieldToValues.hpp"
#include "orm-cxx/relations.hpp"
#include "orm-cxx/utils//ConstexprFor.hpp"
#include "orm-cxx/utils/DisableExternalsWarning.hpp"
#include "soci/type-conversion.h"
#include "soci/values.h"

DISABLE_WARNING_PUSH
DISABLE_EXTERNAL_WARNINGS
#include "rfl/to_view.hpp"
DISABLE_WARNING_POP

namespace soci
{
template <typename T, bool JoinedValues = false>
using BindingPayload = orm::db::binding::BindingPayload<T, JoinedValues>;

template <typename T, bool JoinedValues>
struct type_conversion<BindingPayload<T, JoinedValues>>
{
    using base_type = values;

    [[maybe_unused]] static void from_base(const soci::values& values, indicator /*ind*/,
                                           BindingPayload<T, JoinedValues>& model)
    {
        auto& modelValue = model.value;
        auto modelAsTuple = rfl::to_view(modelValue).values();
        std::size_t columnIndex = 0;

        auto getObjectFromValues = [&model, &values, &columnIndex](auto /*fieldIndex*/, auto* field)
        {
            using field_t = std::decay_t<decltype(*field)>;

            if constexpr (orm::is_relation_collection_v<field_t>)
            {
                return;
            }
            else
            {
                orm::db::binding::ObjectFieldFromValues<field_t>::get(field, model, columnIndex, values);
                ++columnIndex;
            }
        };

        orm::utils::constexpr_for_tuple(modelAsTuple, getObjectFromValues);
    }

    [[maybe_unused]] static void to_base(const BindingPayload<T, JoinedValues>& model, soci::values& values,
                                         indicator& ind)
    {
        auto& modelValue = model.value;
        auto modelAsTuple = rfl::to_view(modelValue).values();
        std::size_t columnIndex = 0;

        auto setObjectToValues = [&model, &values, &columnIndex](auto /*fieldIndex*/, const auto* field)
        {
            using field_t = std::decay_t<decltype(*field)>;

            if constexpr (orm::is_relation_collection_v<field_t>)
            {
                return;
            }
            else
            {
                orm::db::binding::ObjectFieldToValues<field_t>::set(field, model, columnIndex, values);
                ++columnIndex;
            }
        };

        orm::utils::constexpr_for_tuple(modelAsTuple, setObjectToValues);

        ind = i_ok;
    }
};
} // namespace soci
