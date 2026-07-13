#include "orm-cxx/database/sqlite/SqliteDialect.hpp"

#include <format>
#include <stdexcept>

#include "SqliteTypeTranslator.hpp"

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
    if (identifier.empty())
    {
        throw std::invalid_argument{"SQL identifier cannot be empty"};
    }

    std::string quoted{"\""};

    for (const auto character : identifier)
    {
        if (character == '"')
        {
            quoted += "\"\"";
        }
        else
        {
            quoted += character;
        }
    }

    quoted += '"';

    return quoted;
}

auto SqliteDialect::bindMarker(std::string_view logicalName) const -> std::string
{
    if (logicalName.empty() or logicalName.front() == ':')
    {
        throw std::invalid_argument{"Bind parameter name must not be empty or include ':'"};
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
    if (pagination.limit.has_value())
    {
        if (pagination.offset.has_value())
        {
            return std::format(" LIMIT {} OFFSET {}", pagination.limit.value(), pagination.offset.value());
        }

        return std::format(" LIMIT {}", pagination.limit.value());
    }

    if (pagination.offset.has_value())
    {
        return std::format(" LIMIT -1 OFFSET {}", pagination.offset.value());
    }

    return {};
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
