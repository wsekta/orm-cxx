#pragma once

#include "ColumnInfo.hpp"
#include "ForeginIdsInfo.hpp"
#include "IdInfo.hpp"
#include "TableInfo.hpp"

namespace orm::model
{
struct ModelInfo
{
    std::string tableName;
    std::vector<ColumnInfo> columnsInfo;
    std::unordered_set<std::string> idColumnsNames;
    std::unordered_map<std::string, std::unordered_map<std::string, ColumnType>> foreignIdsInfo;
};

template <typename T>
auto generateModelInfo() -> ModelInfo
{
    ModelInfo modelInfo;

    modelInfo.idColumnsNames = getPrimaryIdColumnsNames<T>();
    modelInfo.tableName = getTableName<T>();
    modelInfo.columnsInfo = getColumnsInfo<T>(modelInfo.idColumnsNames);
    modelInfo.foreignIdsInfo = getForeignIdsInfo<T>();

    return modelInfo;
}
}
