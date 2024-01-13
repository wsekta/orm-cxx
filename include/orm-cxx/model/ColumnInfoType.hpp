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
    bool isUnique;
    bool isNotNull;
};
}