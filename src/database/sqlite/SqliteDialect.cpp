module;

#include "soci/soci.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <charconv>
#include <cmath>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

module orm;

import :internal;

namespace
{
auto join(const std::vector<std::string>& values, std::string_view separator) -> std::string
{
    std::string result;

    for (const auto& value : values)
    {
        if (not result.empty())
        {
            result += separator;
        }

        result += value;
    }

    return result;
}
} // namespace

namespace orm::db::sqlite
{
auto SqliteDialect::quoteIdentifier(std::string_view identifier) const -> std::string
{
    return detail::quoteStandardIdentifier(identifier, CompiledSqlFlavor::SQLite);
}

auto SqliteDialect::bindMarker(std::string_view logicalName) const -> std::string
{
    if (not detail::isPortableBindName(logicalName))
    {
        throw std::invalid_argument{"SQLite bind parameter names may contain only ASCII letters, digits, and '_'"};
    }

    return ":" + std::string{logicalName};
}

auto SqliteDialect::toSqlType(model::ColumnType type) const -> std::string
{
    static const SqliteTypeTranslator translator;

    return translator.toSqlType(type);
}

auto SqliteDialect::renderCreateTablePrefix(std::string_view tableName, bool ifNotExists) const -> std::string
{
    return std::format("CREATE TABLE {}{} (", ifNotExists ? "IF NOT EXISTS " : "", quoteIdentifier(tableName));
}

auto SqliteDialect::renderDropTable(std::string_view tableName, bool ifExists) const -> std::string
{
    return std::format("DROP TABLE {}{};", ifExists ? "IF EXISTS " : "", quoteIdentifier(tableName));
}

auto SqliteDialect::renderAutoIncrementPrimaryKey(std::string_view columnName) const -> std::string
{
    return std::format("{} INTEGER PRIMARY KEY AUTOINCREMENT", quoteIdentifier(columnName));
}

auto SqliteDialect::renderPagination(const PaginationSpec& pagination) const -> std::string
{
    detail::SqlQueryView query;
    query.hasLimit = pagination.limit.has_value();
    query.hasOffset = pagination.offset.has_value();
    query.literalLimit = pagination.limit.value_or(0);
    query.literalOffset = pagination.offset.value_or(0);
    return detail::StaticSqlPolicy<CompiledSqlFlavor::SQLite>{}.pagination(query);
}

auto SqliteDialect::renderInsertIfAbsent(const InsertIfAbsentSpec& insert) const -> std::string
{
    if (insert.columns.empty())
    {
        throw std::invalid_argument{"Insert-if-absent requires at least one column"};
    }

    if (insert.columns.size() != insert.valueExpressions.size())
    {
        throw std::invalid_argument{"Insert-if-absent column and value counts must match"};
    }

    if (insert.conflictColumns.empty())
    {
        throw std::invalid_argument{"Insert-if-absent requires conflict columns"};
    }

    std::vector<std::string> columns;
    std::vector<std::string> conflictColumns;
    columns.reserve(insert.columns.size());
    conflictColumns.reserve(insert.conflictColumns.size());

    for (const auto& column : insert.columns)
    {
        columns.push_back(quoteIdentifier(column));
    }

    for (const auto& column : insert.conflictColumns)
    {
        conflictColumns.push_back(quoteIdentifier(column));
    }

    return std::format("INSERT INTO {} ({}) VALUES ({}) ON CONFLICT ({}) DO NOTHING;",
                       quoteIdentifier(insert.tableName), join(columns, ", "), join(insert.valueExpressions, ", "),
                       join(conflictColumns, ", "));
}
} // namespace orm::db::sqlite
