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
    Uuid [[maybe_unused]],
    Unknown,
    OneToOne,
};

auto toColumnType(const std::string& type) -> std::pair<ColumnType, bool>;
} // namespace orm::model
