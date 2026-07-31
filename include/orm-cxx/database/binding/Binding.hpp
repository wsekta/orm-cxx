#pragma once

#include "BindingPayload.hpp"
#include "ObjectFieldFromValues.hpp"
#include "ObjectFieldToValues.hpp"
#include "orm-cxx/reflection/Reflection.hpp"
#include "orm-cxx/relations.hpp"
#include "orm-cxx/utils//ConstexprFor.hpp"
#include "soci/type-conversion.h"
#include "soci/values.h"

namespace soci
{
template <typename T, typename SchemaType, bool JoinedValues = false>
using BindingPayload = orm::db::binding::BindingPayload<T, SchemaType, JoinedValues>;

template <typename T, typename SchemaType, bool JoinedValues>
struct type_conversion<BindingPayload<T, SchemaType, JoinedValues>>
{
    using base_type = values;

    [[maybe_unused]] static void from_base(const soci::values& values, indicator /*ind*/,
                                           BindingPayload<T, SchemaType, JoinedValues>& model)
    {
        auto& modelValue = model.value;
        auto modelAsTuple = orm::reflection::fieldPointers(modelValue);

        auto getObjectFromValues = [&model, &values](auto fieldIndex, auto* field)
        {
            using field_t = std::decay_t<decltype(*field)>;

            if constexpr (orm::is_relation_collection_v<field_t>)
            {
                return;
            }
            else
            {
                orm::db::binding::ObjectFieldFromValues<field_t>::get(field, model, fieldIndex, values);
            }
        };

        orm::utils::constexpr_for_tuple(modelAsTuple, getObjectFromValues);
    }

    [[maybe_unused]] static void to_base(const BindingPayload<T, SchemaType, JoinedValues>& model, soci::values& values,
                                         indicator& ind)
    {
        auto& modelValue = model.value;
        auto modelAsTuple = orm::reflection::fieldPointers(modelValue);

        auto setObjectToValues = [&model, &values](auto fieldIndex, const auto* field)
        {
            using field_t = std::decay_t<decltype(*field)>;

            if constexpr (orm::is_relation_collection_v<field_t>)
            {
                return;
            }
            else
            {
                orm::db::binding::ObjectFieldToValues<field_t>::set(field, model, fieldIndex, values);
            }
        };

        orm::utils::constexpr_for_tuple(modelAsTuple, setObjectToValues);

        ind = i_ok;
    }
};
} // namespace soci
