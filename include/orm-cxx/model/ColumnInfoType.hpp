#pragma once

#include <string>

#include "ColumnType.hpp"

namespace orm::model
{
struct ColumnInfo
{
    std::string name;
    ColumnType type;
    bool isPrimaryKey;
    bool isForeignModel;
    [[maybe_unused]] bool isUnique;
    bool isNotNull;
};
} // namespace orm::model
