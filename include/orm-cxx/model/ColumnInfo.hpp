#pragma once

#include <regex>
#include <rfl.hpp>
#include <string>
#include <unordered_set>
#include <vector>

#include "ColumnInfoType.hpp"
#include "ColumnType.hpp"
#include "ForeginIdsInfoType.hpp"
#include "NameMapping.hpp"

namespace orm::model
{
template <typename T>
auto getColumnsInfo(const std::unordered_set<std::string>& ids, const ForeignIdsInfo& foreignIdsInfo)
    -> std::vector<ColumnInfo>
{
    auto fields = rfl::fields<T>();

    std::vector<ColumnInfo> columnsInfo{};

    for (const auto& field : fields)
    {
        ColumnInfo columnInfo{};

        columnInfo.name = getColumnName<T>(field.name());

        auto [type, isNotNull] = toColumnType(field.type());
        
        columnInfo.type = type;

        if (type == ColumnType::Unknown and foreignIdsInfo.contains(field.name()))
        {
            columnInfo.isForeignModel = true;
            columnInfo.type = ColumnType::OneToMany;
        }

        columnInfo.isNotNull = isNotNull;

        if (ids.contains(columnInfo.name))
        {
            columnInfo.isPrimaryKey = true;
        }

        columnsInfo.push_back(columnInfo);
    }

    return columnsInfo;
}
}