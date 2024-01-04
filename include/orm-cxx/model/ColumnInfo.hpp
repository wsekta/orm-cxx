#pragma once

#include <rfl.hpp>
#include <string>
#include <vector>

namespace orm::model
{
struct ColumnInfo
{
    std::string name;
    std::string type;
    std::string defaultValue;
    bool isPrimaryKey;
    bool isForeignKey;
    bool isUnique;
    bool isNotNull;
};

template <typename T>
auto getColumnsInfo() -> std::vector<ColumnInfo>
{
    auto fields = rfl::fields<T>();

    std::vector<ColumnInfo> columnsInfo;

    for (const auto& field : fields)
    {
        ColumnInfo columnInfo;

        columnInfo.name = field.name();
        columnInfo.type = field.type();

        columnsInfo.push_back(columnInfo);
    }

    return columnsInfo;
}
}