#pragma once

#include <stdexcept>
#include <string>
#include <unordered_set>

#include "ModelInfo.hpp"
#include "NameMapping.hpp"

namespace orm::model
{
template <typename T>
auto getAutoIncrementColumnsNames() -> std::unordered_set<std::string>
{
    if constexpr (requires { T::auto_increment_columns; })
    {
        std::unordered_set<std::string> columns{};

        for (const auto& column : T::auto_increment_columns)
        {
            columns.insert(getColumnName<T>(column));
        }

        return columns;
    }
    else
    {
        return {};
    }
}

inline auto findColumnInfo(ModelInfo& modelInfo, const std::string& columnName) -> ColumnInfo*
{
    for (auto& columnInfo : modelInfo.columnsInfo)
    {
        if (columnInfo.name == columnName)
        {
            return &columnInfo;
        }
    }

    return nullptr;
}

inline auto markAutoIncrementColumns(ModelInfo& modelInfo,
                                     const std::unordered_set<std::string>& autoIncrementColumnsNames) -> void
{
    for (const auto& autoIncrementColumnName : autoIncrementColumnsNames)
    {
        auto* const columnInfo = findColumnInfo(modelInfo, autoIncrementColumnName);

        if (columnInfo == nullptr)
        {
            throw std::invalid_argument{"Unknown auto-increment column: " + autoIncrementColumnName};
        }

        columnInfo->isAutoIncrement = true;
    }
}

inline auto validateAutoIncrementColumns(const ModelInfo& modelInfo) -> void
{
    std::size_t autoIncrementColumnsCount{};

    for (const auto& columnInfo : modelInfo.columnsInfo)
    {
        if (columnInfo.isAutoIncrement)
        {
            ++autoIncrementColumnsCount;
        }
    }

    if (autoIncrementColumnsCount == 0)
    {
        return;
    }

    if (autoIncrementColumnsCount > 1)
    {
        throw std::invalid_argument{"Only one auto-increment column is supported"};
    }

    if (modelInfo.idColumnsNames.size() != 1)
    {
        throw std::invalid_argument{"Auto-increment requires exactly one primary key column"};
    }

    for (const auto& columnInfo : modelInfo.columnsInfo)
    {
        if (not columnInfo.isAutoIncrement)
        {
            continue;
        }

        if (not columnInfo.isPrimaryKey)
        {
            throw std::invalid_argument{"Auto-increment column must be a primary key: " + columnInfo.fieldName};
        }

        if (columnInfo.isForeignModel)
        {
            throw std::invalid_argument{"Auto-increment column cannot be a related model: " + columnInfo.fieldName};
        }

        if (not columnInfo.isNotNull)
        {
            throw std::invalid_argument{"Auto-increment column cannot be optional: " + columnInfo.fieldName};
        }

        if (columnInfo.type != ColumnType::Int)
        {
            throw std::invalid_argument{"Auto-increment column must have int type: " + columnInfo.fieldName};
        }
    }
}

inline auto applyAutoIncrementColumns(ModelInfo& modelInfo,
                                      const std::unordered_set<std::string>& autoIncrementColumnsNames) -> void
{
    markAutoIncrementColumns(modelInfo, autoIncrementColumnsNames);
    validateAutoIncrementColumns(modelInfo);
}
} // namespace orm::model
