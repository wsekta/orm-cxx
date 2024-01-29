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
    Uuid [[maybe_unused]],
    Unknown,
    OneToOne,
};

auto toColumnType(const std::string& type) -> std::pair<ColumnType, bool>;
} // namespace orm::model
