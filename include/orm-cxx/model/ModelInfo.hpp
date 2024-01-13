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
    ForeignIdsInfo foreignIdsInfo;
};

template <typename T>
auto generateModelInfo() -> ModelInfo
{
    ModelInfo modelInfo;

    modelInfo.idColumnsNames = getPrimaryIdColumnsNames<T>();
    modelInfo.tableName = getTableName<T>();
    modelInfo.foreignIdsInfo = getForeignIdsInfo<T>();
    modelInfo.columnsInfo = getColumnsInfo<T>(modelInfo.idColumnsNames, modelInfo.foreignIdsInfo);

    return modelInfo;
}
}
