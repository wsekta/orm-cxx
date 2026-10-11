module;

#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

module orm;

import :internal;

namespace orm::db::mariadb
{
auto MariadbDialect::quoteIdentifier(std::string_view name) const -> std::string
{
    return detail::quoteStandardIdentifier(name, CompiledSqlFlavor::MySQLFamily);
}
auto MariadbDialect::bindMarker(std::string_view name) const -> std::string
{
    return detail::StaticSqlPolicy<CompiledSqlFlavor::MySQLFamily>{}.bindMarker(name);
}
auto MariadbDialect::toSqlType(model::ColumnType type, std::optional<std::size_t> maxStringLength) const -> std::string
{
    return MariadbTypeTranslator{}.toSqlType(type, maxStringLength);
}
auto MariadbDialect::renderCreateTablePrefix(std::string_view name, bool ifNotExists) const -> std::string
{
    return std::format("CREATE TABLE {}{} (", ifNotExists ? "IF NOT EXISTS " : "", quoteIdentifier(name));
}
auto MariadbDialect::renderCreateTableSuffix() const -> std::string
{
    return "\n) ENGINE=InnoDB DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_nopad_bin;";
}
auto MariadbDialect::renderDefaultInsert(std::string_view name) const -> std::string
{
    return std::format("INSERT INTO {} () VALUES ();", quoteIdentifier(name));
}
auto MariadbDialect::renderDropTable(std::string_view name, bool ifExists) const -> std::string
{
    return std::format("DROP TABLE {}{};", ifExists ? "IF EXISTS " : "", quoteIdentifier(name));
}
auto MariadbDialect::renderAutoIncrementPrimaryKey(std::string_view name) const -> std::string
{
    return std::format("{} INT NOT NULL AUTO_INCREMENT PRIMARY KEY", quoteIdentifier(name));
}
auto MariadbDialect::renderPagination(const PaginationSpec& pagination) const -> std::string
{
    detail::SqlQueryView query;
    query.hasLimit = pagination.limit.has_value();
    query.hasOffset = pagination.offset.has_value();
    query.literalLimit = pagination.limit.value_or(0);
    query.literalOffset = pagination.offset.value_or(0);
    return detail::StaticSqlPolicy<CompiledSqlFlavor::MySQLFamily>{}.pagination(query);
}
auto MariadbDialect::renderInsertIfAbsent(const InsertIfAbsentSpec& insert) const -> std::string
{
    return mysql_family::renderInsert(insert);
}
auto MariadbDialect::renderAggregateResult(std::string_view expression, bool exact) const -> std::string
{
    return detail::StaticSqlPolicy<CompiledSqlFlavor::MySQLFamily>{}.renderAggregateResult(expression, exact);
}
}
