#pragma once

#include <format>
#include <string>
#include <string_view>

#include "orm-cxx/database/SqlDialect.hpp"

namespace orm::db::aliases
{
[[nodiscard]] inline auto qualifiedIdentifier(const SqlDialect& dialect, std::string_view qualifier,
                                              std::string_view identifier) -> std::string
{
    return std::format("{}.{}", dialect.quoteIdentifier(qualifier), dialect.quoteIdentifier(identifier));
}

[[nodiscard]] inline auto modelColumn(std::string_view tableName, std::string_view columnName) -> std::string
{
    return std::format("{}_{}", tableName, columnName);
}

[[nodiscard]] inline auto joinedRelationColumn(std::string_view relationName, std::string_view columnName)
    -> std::string
{
    return std::format("{}_{}", relationName, columnName);
}

[[nodiscard]] inline auto unjoinedRelationColumn(std::string_view tableName, std::string_view relationName,
                                                 std::string_view columnName) -> std::string
{
    return std::format("{}_{}_{}", tableName, relationName, columnName);
}
} // namespace orm::db::aliases
