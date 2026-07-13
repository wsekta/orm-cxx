#pragma once

#include <utility>

#include "Binding.hpp"
#include "PrimaryKey.hpp"
#include "soci/type-conversion.h"

namespace orm::db::binding
{
template <typename Owner, typename Target, bool JoinedValues>
struct CollectionPayload
{
    mutable Target value;
    PrimaryKey ownerKey;
};
} // namespace orm::db::binding

namespace soci
{
template <typename Owner, typename Target, bool JoinedValues>
struct type_conversion<orm::db::binding::CollectionPayload<Owner, Target, JoinedValues>>
{
    using base_type = values;

    [[maybe_unused]] static void from_base(const soci::values& values, indicator ind,
                                           orm::db::binding::CollectionPayload<Owner, Target, JoinedValues>& payload)
    {
        orm::db::binding::BindingPayload<Target, JoinedValues> targetPayload{};
        type_conversion<orm::db::binding::BindingPayload<Target, JoinedValues>>::from_base(values, ind, targetPayload);
        payload.value = std::move(targetPayload.value);

        const auto& ownerInfo = orm::Model<Owner>::getModelInfo();
        const auto ownerColumns = orm::db::binding::getPrimaryKeyColumns(ownerInfo);
        payload.ownerKey.clear();
        payload.ownerKey.reserve(ownerColumns.size());

        for (const auto* column : ownerColumns)
        {
            const auto alias = orm::db::binding::relationOwnerAlias(*column);
            payload.ownerKey.push_back(orm::db::binding::getPrimaryKeyValue(values, alias, column->type));
        }
    }

    [[maybe_unused]] static void
    to_base(const orm::db::binding::CollectionPayload<Owner, Target, JoinedValues>& /*payload*/,
            soci::values& /*values*/, indicator& ind)
    {
        ind = i_ok;
    }
};
} // namespace soci
