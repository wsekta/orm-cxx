#pragma once

#include <regex>
#include <rfl.hpp>
#include <string>
#include <unordered_set>
#include <vector>

namespace
{
const std::regex optionalRegex("(class )?std\\:\\:optional\\<(.*)\\>");
}

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
auto getColumnsInfo(const std::unordered_set<std::string>& ids = {}) -> std::vector<ColumnInfo>
{
    auto fields = rfl::fields<T>();

    std::vector<ColumnInfo> columnsInfo{};

    for (const auto& field : fields)
    {
        ColumnInfo columnInfo{};

        columnInfo.name = field.name();

        if (std::regex_match(field.type(), optionalRegex))
        {
            columnInfo.type = std::regex_replace(field.type(), optionalRegex, "$2");
            columnInfo.isNotNull = false;
        }
        else
        {
            columnInfo.type = field.type();
            columnInfo.isNotNull = true;
        }

        if (ids.contains(field.name()))
        {
            columnInfo.isPrimaryKey = true;
        }

        columnsInfo.push_back(columnInfo);
    }

    return columnsInfo;
}
}