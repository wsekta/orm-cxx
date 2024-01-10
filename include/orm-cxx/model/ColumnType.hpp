#pragma once

#include <string>

namespace orm::model
{
enum class ColumnType
{
    Char,
    Int,
    Float,
    Double,
    String,
    Uuid,
};

std::pair<ColumnType, bool> toColumnType(const std::string& type);
}
