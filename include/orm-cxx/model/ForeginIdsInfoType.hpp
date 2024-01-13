#pragma once

#include <string>
#include <unordered_map>

#include "ColumnInfoType.hpp"

namespace orm::model
{
struct ForeignIdInfo
{
    std::string tableName;
    std::unordered_map<std::string, ColumnInfo> idFields;
};

using ForeignIdsInfo = std::unordered_map<std::string, ForeignIdInfo>;
}