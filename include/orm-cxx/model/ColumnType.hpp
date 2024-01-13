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
    Unknown,
    OneToMany,
};

auto toColumnType(const std::string& type) -> std::pair<ColumnType, bool>;
}
