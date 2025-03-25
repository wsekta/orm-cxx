#pragma once

#include "ColumnInfo.hpp"
#include "TableInfo.hpp"

namespace orm::model
{
struct ModelInfo
{
    std::string_view tableName;
    std::vector<ColumnInfo> columnsInfo;
    std::unordered_set<std::string> idColumnsNames;
    std::unordered_map<std::string, ModelInfo> foreignModelsInfo;
};
} // namespace orm::model
