#pragma once

#include "ColumnInfo.hpp"
#include "TableInfo.hpp"

namespace orm::model
{
struct ModelInfo
{
    std::string tableName;
    std::vector<ColumnInfo> columnsInfo;
};

template <typename T>
auto generateModelInfo() -> ModelInfo
{
    ModelInfo modelInfo;

    modelInfo.tableName = getTableName<T>();
    modelInfo.columnsInfo = getColumnsInfo<T>();

    return modelInfo;
}
}
