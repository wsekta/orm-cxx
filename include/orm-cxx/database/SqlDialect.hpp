#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "orm-cxx/model/ColumnType.hpp"

namespace orm::db
{
struct PaginationSpec
{
    std::optional<std::size_t> limit;
    std::optional<std::size_t> offset;
    bool hasOrderBy = false;

    auto operator==(const PaginationSpec&) const -> bool = default;
};

struct InsertIfAbsentSpec
{
    std::string tableName;
    std::vector<std::string> columns;
    std::vector<std::string> valueExpressions;
    std::vector<std::string> conflictColumns;

    auto operator==(const InsertIfAbsentSpec&) const -> bool = default;
};

class SqlDialect
{
public:
    virtual ~SqlDialect() = default;

    [[nodiscard]] virtual auto quoteIdentifier(std::string_view identifier) const -> std::string = 0;
    [[nodiscard]] virtual auto bindMarker(std::string_view logicalName) const -> std::string = 0;
    [[nodiscard]] virtual auto toSqlType(model::ColumnType type) const -> std::string = 0;
    [[nodiscard]] virtual auto renderCreateTablePrefix(std::string_view tableName, bool ifNotExists) const
        -> std::string = 0;
    [[nodiscard]] virtual auto renderDropTable(std::string_view tableName, bool ifExists) const -> std::string = 0;
    [[nodiscard]] virtual auto renderAutoIncrementPrimaryKey(std::string_view columnName) const -> std::string = 0;
    [[nodiscard]] virtual auto renderPagination(const PaginationSpec& pagination) const -> std::string = 0;
    [[nodiscard]] virtual auto renderInsertIfAbsent(const InsertIfAbsentSpec& insert) const -> std::string = 0;
    [[nodiscard]] virtual auto renderAggregateResult(std::string_view expression, bool /*preserveExactNumeric*/) const
        -> std::string
    {
        return std::string{expression};
    }
};
} // namespace orm::db
