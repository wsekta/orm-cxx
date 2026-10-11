#pragma once

#include <format>
#include <string>
#include <string_view>

namespace orm::tests
{
/** SQL generation fixture independent of every installed database driver. */
class TestSqlDialect : public db::SqlDialect
{
public:
    auto quoteIdentifier(std::string_view name) const -> std::string override
    {
        return db::detail::quoteStandardIdentifier(name, db::CompiledSqlFlavor::SQLite);
    }
    auto bindMarker(std::string_view name) const -> std::string override
    {
        return ":" + std::string{name};
    }
    auto toSqlType(model::ColumnType type, std::optional<std::size_t> = std::nullopt) const -> std::string override
    {
        switch (type)
        {
        case model::ColumnType::Bool:
            return "BOOLEAN";
        case model::ColumnType::Char:
        case model::ColumnType::UnsignedChar:
            return "TINYINT";
        case model::ColumnType::Short:
        case model::ColumnType::UnsignedShort:
            return "SMALLINT";
        case model::ColumnType::Int:
            return "INTEGER";
        case model::ColumnType::LongLong:
            return "BIGINT";
        case model::ColumnType::UnsignedInt:
        case model::ColumnType::UnsignedLongLong:
            return "UNSIGNED BIG INT";
        case model::ColumnType::Float:
            return "REAL";
        case model::ColumnType::Double:
            return "REAL";
        case model::ColumnType::String:
            return "TEXT";
        case model::ColumnType::Uuid:
            break;
        }
        throw std::invalid_argument{"Unsupported SQL test type"};
    }
    auto renderCreateTablePrefix(std::string_view table, bool conditional) const -> std::string override
    {
        return std::format("CREATE TABLE {}{} (", conditional ? "IF NOT EXISTS " : "", quoteIdentifier(table));
    }
    auto renderDropTable(std::string_view table, bool conditional) const -> std::string override
    {
        return std::format("DROP TABLE {}{};", conditional ? "IF EXISTS " : "", quoteIdentifier(table));
    }
    auto renderAutoIncrementPrimaryKey(std::string_view column) const -> std::string override
    {
        return quoteIdentifier(column) + " INTEGER PRIMARY KEY AUTOINCREMENT";
    }
    auto renderPagination(const db::PaginationSpec& spec) const -> std::string override
    {
        std::string result;
        if (spec.limit)
            result += " LIMIT " + std::to_string(*spec.limit);
        else if (spec.offset)
            result += " LIMIT -1";
        if (spec.offset)
            result += " OFFSET " + std::to_string(*spec.offset);
        return result;
    }
    auto renderInsertIfAbsent(const db::InsertIfAbsentSpec& spec) const -> std::string override
    {
        std::string result = "INSERT INTO " + quoteIdentifier(spec.tableName) + " (";
        for (std::size_t i = 0; i < spec.columns.size(); ++i)
            result += (i == 0 ? "" : ", ") + quoteIdentifier(spec.columns[i]);
        result += ") VALUES (";
        for (std::size_t i = 0; i < spec.valueExpressions.size(); ++i)
            result += (i == 0 ? "" : ", ") + spec.valueExpressions[i];
        result += ") ON CONFLICT (";
        for (std::size_t i = 0; i < spec.conflictColumns.size(); ++i)
            result += (i == 0 ? "" : ", ") + quoteIdentifier(spec.conflictColumns[i]);
        return result + ") DO NOTHING;";
    }
};

class SnapshotSqlDialect final : public TestSqlDialect
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

    [[nodiscard]] auto
    toSqlType(model::ColumnType type,
              std::optional<std::size_t> maxStringLength = std::nullopt) const -> std::string override
    {
        return TestSqlDialect::toSqlType(type, maxStringLength);
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
        return TestSqlDialect::renderPagination(pagination);
    }

    [[nodiscard]] auto renderInsertIfAbsent(const db::InsertIfAbsentSpec& insert) const -> std::string override
    {
        return TestSqlDialect::renderInsertIfAbsent(insert);
    }
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

    [[nodiscard]] auto
    toSqlType(model::ColumnType /*type*/,
              std::optional<std::size_t> /*maxStringLength*/ = std::nullopt) const -> std::string override
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

    [[nodiscard]] auto renderAggregateResult(std::string_view expression,
                                             bool preserveExactNumeric) const -> std::string override
    {
        if (preserveExactNumeric)
        {
            return "EXACT_RESULT(" + std::string{expression} + ")";
        }

        return std::string{expression};
    }
};

class TestRuntime final : public db::BackendRuntime
{
public:
    auto open(soci::session&, std::string_view) const -> void override
    {
        throw std::logic_error{"SQL fixture has no driver"};
    }
    auto onConnect(soci::session&) const -> void override {}
    auto tableExists(soci::session&, std::string_view) const -> bool override
    {
        return false;
    }
    auto limits(soci::session&) const -> db::BackendRuntimeLimits override
    {
        return {};
    }
    auto normalizeAffectedRows(long long rows) const -> std::size_t override
    {
        if (rows < 0)
            throw std::invalid_argument{"Missing affected rows"};
        return static_cast<std::size_t>(rows);
    }
    auto bind(soci::values&, std::string_view, const db::BoundValue&) const -> void override {}
    auto translateError(const soci::soci_error&, DatabaseErrorCode code,
                        std::string_view operation) const -> DatabaseError override
    {
        return DatabaseError{code, db::BackendType::Oracle, std::string{operation}, "SQL fixture error"};
    }
};

class TestBackend final : public db::BackendProvider
{
public:
    explicit TestBackend(db::BackendType type = db::BackendType::Oracle, bool accepts = false)
        : type_{type}, accepts_{accepts}, generator_{db::defaults::makeDefaultCommandGenerator(dialect_)}
    {
    }
    auto type() const noexcept -> db::BackendType override
    {
        return type_;
    }
    auto acceptsConnectionString(std::string_view) const noexcept -> bool override
    {
        return accepts_;
    }
    auto capabilities() const noexcept -> const db::BackendCapabilities& override
    {
        return capabilities_;
    }
    auto dialect() const noexcept -> const db::SqlDialect& override
    {
        return dialect_;
    }
    auto runtime() const noexcept -> const db::BackendRuntime& override
    {
        return runtime_;
    }
    auto commandGenerator() const noexcept -> const db::CommandGenerator& override
    {
        return *generator_;
    }

private:
    db::BackendType type_;
    bool accepts_;
    db::BackendCapabilities capabilities_;
    TestSqlDialect dialect_;
    TestRuntime runtime_;
    std::unique_ptr<db::CommandGenerator> generator_;
};
} // namespace orm::tests
