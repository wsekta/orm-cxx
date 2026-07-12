#pragma once

#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ColumnInfoType.hpp"
#include "ColumnType.hpp"
#include "NameMapping.hpp"
#include "orm-cxx/relations.hpp"
#include "orm-cxx/utils/ConstexprFor.hpp"
#include "orm-cxx/utils/DisableExternalsWarning.hpp"

DISABLE_WARNING_PUSH

DISABLE_EXTERNAL_WARNINGS

#include <rfl.hpp>

DISABLE_WARNING_POP

namespace orm::model
{
template <typename T>
auto getColumnsInfo(const std::unordered_set<std::string>& ids) -> std::vector<ColumnInfo>
{
    auto fields = rfl::fields<T>();

    std::vector<ColumnInfo> columnsInfo{};
    using model_tuple_t = std::decay_t<decltype(rfl::to_view(std::declval<T&>()).values())>;

    auto appendColumn = [&columnsInfo, &fields, &ids](auto i, auto fieldPointer)
    {
        using field_t = std::decay_t<decltype(*fieldPointer)>;

        if constexpr (is_relation_collection_v<field_t> || is_optional_relation_collection_v<field_t>)
        {
            return;
        }
        else
        {
            const auto& field = fields[i];
            ColumnInfo columnInfo{};

            columnInfo.fieldName = field.name();

            columnInfo.name = getColumnName<T>(columnInfo.fieldName);

            auto [type, isNotNull] = toColumnType(field.type());

            columnInfo.type = type;

            columnInfo.isNotNull = isNotNull;

            if (ids.contains(columnInfo.name))
            {
                columnInfo.isPrimaryKey = true;
            }

            columnsInfo.push_back(columnInfo);
        }
    };

    utils::constexpr_for_tuple<model_tuple_t>(appendColumn);

    return columnsInfo;
}
} // namespace orm::model
