#pragma once

#include <format>
#include <string>
#include <string_view>

#include "orm-cxx/database/sqlite/SqliteDialect.hpp"

namespace orm::tests
{
class SnapshotSqliteDialect final : public db::SqlDialect
{
public:
    [[nodiscard]] auto quoteIdentifier(std::string_view identifier) const -> std::string override
    {
        return std::string{identifier};
    }

    [[nodiscard]] auto bindMarker(std::string_view logicalName) const -> std::string override
    {
        return ":" + std::string{logicalName};
    }

    [[nodiscard]] auto toSqlType(model::ColumnType type) const -> std::string override
    {
        return sqlite.toSqlType(type);
    }

    [[nodiscard]] auto renderCreateTablePrefix(std::string_view tableName,
                                               bool ifNotExists) const -> std::string override
    {
        return std::format("CREATE TABLE {}{} (", ifNotExists ? "IF NOT EXISTS " : "", tableName);
    }

    [[nodiscard]] auto renderDropTable(std::string_view tableName, bool ifExists) const -> std::string override
    {
        return std::format("DROP TABLE {}{};", ifExists ? "IF EXISTS " : "", tableName);
    }

    [[nodiscard]] auto renderAutoIncrementPrimaryKey(std::string_view columnName) const -> std::string override
    {
        return std::format("{} INTEGER PRIMARY KEY AUTOINCREMENT", columnName);
    }

    [[nodiscard]] auto renderPagination(const db::PaginationSpec& pagination) const -> std::string override
    {
        return sqlite.renderPagination(pagination);
    }

    [[nodiscard]] auto renderInsertIfAbsent(const db::InsertIfAbsentSpec& insert) const -> std::string override
    {
        return sqlite.renderInsertIfAbsent(insert);
    }

private:
    db::sqlite::SqliteDialect sqlite;
};

class TrackingSqlDialect final : public db::SqlDialect
{
public:
    [[nodiscard]] auto quoteIdentifier(std::string_view identifier) const -> std::string override
    {
        return "[" + std::string{identifier} + "]";
    }

    [[nodiscard]] auto bindMarker(std::string_view logicalName) const -> std::string override
    {
        return "$" + std::string{logicalName};
    }

    [[nodiscard]] auto toSqlType(model::ColumnType /*type*/) const -> std::string override
    {
        return "PORTABLE_TYPE";
    }

    [[nodiscard]] auto renderCreateTablePrefix(std::string_view tableName,
                                               bool ifNotExists) const -> std::string override
    {
        return std::string{ifNotExists ? "CREATE_PORTABLE_IF_ABSENT " : "CREATE_PORTABLE "} +
               quoteIdentifier(tableName) + " (";
    }

    [[nodiscard]] auto renderDropTable(std::string_view tableName, bool ifExists) const -> std::string override
    {
        return std::string{ifExists ? "DROP_PORTABLE_IF_PRESENT " : "DROP_PORTABLE "} + quoteIdentifier(tableName) +
               ";";
    }

    [[nodiscard]] auto renderAutoIncrementPrimaryKey(std::string_view columnName) const -> std::string override
    {
        return "PORTABLE_AUTO " + quoteIdentifier(columnName);
    }

    [[nodiscard]] auto renderPagination(const db::PaginationSpec& /*pagination*/) const -> std::string override
    {
        return {};
    }

    [[nodiscard]] auto renderInsertIfAbsent(const db::InsertIfAbsentSpec& /*insert*/) const -> std::string override
    {
        return "INSERT_PORTABLE_IF_ABSENT;";
    }
};
} // namespace orm::tests
