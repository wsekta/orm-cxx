#pragma once

#include "orm-cxx/database/SqlDialect.hpp"

namespace orm::db::postgresql
{
class PostgresqlDialect final : public SqlDialect
{
public:
    [[nodiscard]] auto quoteIdentifier(std::string_view identifier) const -> std::string override;
    [[nodiscard]] auto bindMarker(std::string_view logicalName) const -> std::string override;
    [[nodiscard]] auto toSqlType(model::ColumnType type) const -> std::string override;
    [[nodiscard]] auto renderCreateTablePrefix(std::string_view tableName, bool ifNotExists) const
        -> std::string override;
    [[nodiscard]] auto renderDropTable(std::string_view tableName, bool ifExists) const -> std::string override;
    [[nodiscard]] auto renderAutoIncrementPrimaryKey(std::string_view columnName) const -> std::string override;
    [[nodiscard]] auto renderPagination(const PaginationSpec& pagination) const -> std::string override;
    [[nodiscard]] auto renderInsertIfAbsent(const InsertIfAbsentSpec& insert) const -> std::string override;
    [[nodiscard]] auto renderAggregateResult(std::string_view expression, bool preserveExactNumeric) const
        -> std::string override;
};
} // namespace orm::db::postgresql
