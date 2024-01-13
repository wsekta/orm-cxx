#pragma once

#include <string>
#include <unordered_map>

#include "ColumnType.hpp"

namespace orm::model
{
using ForeignIdsInfo = std::unordered_map<std::string, std::unordered_map<std::string, ColumnType>>;

template <typename T>
auto getForeignIdsInfo() -> ForeignIdsInfo
{
    ForeignIdsInfo foreignIdsInfo{};

    return {};
}
}