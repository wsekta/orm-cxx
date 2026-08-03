#pragma once

#include <utility>

#include "Binding.hpp"
#include "PrimaryKey.hpp"
#include "soci/type-conversion.h"

namespace orm::db::binding
{
template <typename Owner, typename Target, typename SchemaType, bool JoinedValues>
struct CollectionPayload
{
    static_assert(SchemaType::template contains<Owner> && SchemaType::template contains<Target>,
                  "Collection binding endpoints must belong to the selected Schema");
    mutable Target value;
    PrimaryKey ownerKey;
};
} // namespace orm::db::binding

namespace soci
{
template <typename Owner, typename Target, typename SchemaType, bool JoinedValues>
struct type_conversion<orm::db::binding::CollectionPayload<Owner, Target, SchemaType, JoinedValues>>
{
    using base_type = values;

    [[maybe_unused]] static void
    from_base(const soci::values& values, indicator ind,
              orm::db::binding::CollectionPayload<Owner, Target, SchemaType, JoinedValues>& payload)
    {
        orm::db::binding::BindingPayload<Target, SchemaType, JoinedValues> targetPayload{};
        type_conversion<orm::db::binding::BindingPayload<Target, SchemaType, JoinedValues>>::from_base(values, ind,
                                                                                                       targetPayload);
        payload.value = std::move(targetPayload.value);

        constexpr auto owner = orm::model::modelView<SchemaType, Owner>();
        const auto ownerColumns = orm::db::binding::getPrimaryKeyColumns(owner);
        payload.ownerKey.clear();
        payload.ownerKey.reserve(ownerColumns.size());

        for (const auto* column : ownerColumns)
        {
            const auto alias = orm::db::binding::relationOwnerAlias(*column);
            payload.ownerKey.push_back(orm::db::binding::getPrimaryKeyValue(values, alias, column->type.value()));
        }
    }

    [[maybe_unused]] static void
    to_base(const orm::db::binding::CollectionPayload<Owner, Target, SchemaType, JoinedValues>& /*payload*/,
            soci::values& /*values*/, indicator& ind)
    {
        ind = i_ok;
    }
};
} // namespace soci
