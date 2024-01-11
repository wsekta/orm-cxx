#pragma once

#include <regex>
#include <rfl.hpp>
#include <string>
#include <unordered_set>
#include <vector>

#include "ColumnType.hpp"
#include "NameMapping.hpp"

namespace orm::model
{
struct ColumnInfo
{
    std::string name;
    ColumnType type;
    bool isPrimaryKey;
    bool isForeignKey;
    bool isUnique;
    bool isNotNull;
};

template <typename T>
auto getColumnsInfo(const std::unordered_set<std::string>& ids = {}) -> std::vector<ColumnInfo>
{
    auto fields = rfl::fields<T>();

    std::vector<ColumnInfo> columnsInfo{};

    for (const auto& field : fields)
    {
        ColumnInfo columnInfo{};

        columnInfo.name = getColumnName<T>(field.name());

        auto [type, isNotNull] = toColumnType(field.type());
        columnInfo.type = type;
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