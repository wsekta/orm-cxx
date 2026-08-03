#pragma once

#include <string>

namespace orm::model
{
enum class ColumnType
{
    Bool,
    Char,
    UnsignedChar,
    Short,
    UnsignedShort,
    Int,
    UnsignedInt,
    LongLong,
    UnsignedLongLong,
    Float,
    Double,
    String,
    Uuid,
};

auto toString(ColumnType type) -> std::string;
} // namespace orm::model
