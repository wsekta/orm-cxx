#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ColumnInfo.hpp"
#include "RelationInfo.hpp"
#include "TableInfo.hpp"

namespace orm::model
{
struct ModelInfo
{
    std::string_view tableName;
    std::vector<ColumnInfo> columnsInfo;
    std::unordered_set<std::string> idColumnsNames;
    std::unordered_map<std::string, ModelInfo> foreignModelsInfo;
    std::vector<RelationInfo> relationsInfo;

    [[nodiscard]] auto findRelation(std::string_view fieldName) const -> const RelationInfo*
    {
        for (const auto& relation : relationsInfo)
        {
            if (relation.fieldName == fieldName || relation.columnName == fieldName)
            {
                return &relation;
            }
        }
        return nullptr;
    }
};
} // namespace orm::model
