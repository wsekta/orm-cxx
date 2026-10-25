module;

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module orm;

import :migrations_internal;

namespace orm::migrations
{
GenerationError::GenerationError(std::string message, std::vector<GenerationDiagnostic> diagnostics)
    : std::runtime_error{std::move(message)}, diagnostics_{std::move(diagnostics)}
{
}

auto GenerationError::diagnostics() const noexcept -> const std::vector<GenerationDiagnostic>&
{
    return diagnostics_;
}

namespace
{
[[noreturn]] auto invalid(std::string message, db::BackendType backend = db::BackendType::Empty,
                          std::string path = {}) -> void
{
    const auto diagnostic = GenerationDiagnostic{
        .code = "invalid-snapshot", .message = message, .path = std::move(path), .backend = backend};
    throw GenerationError{std::move(message), {diagnostic}};
}

auto requireBackend(db::BackendType backend) -> void
{
    if (std::ranges::find(generationBackends, backend) == generationBackends.end())
    {
        invalid("Unsupported schema-generation backend", backend);
    }
}

auto family(db::BackendType backend) -> bool
{
    return backend == db::BackendType::Mysql || backend == db::BackendType::Mariadb;
}

auto profile(db::BackendType backend) -> std::string
{
    requireBackend(backend);
    return "orm-cxx-ddl-v1:" + std::string{detail::backendName(backend)};
}

auto validName(std::string_view name) -> bool
{
    return !name.empty() && name.find('\0') == std::string_view::npos;
}

auto requireName(std::string_view name, db::BackendType backend, std::string_view path) -> void
{
    if (!validName(name))
    {
        invalid("SQL identifiers must be nonempty and cannot contain NUL", backend, std::string{path});
    }
    if (name.starts_with("__orm_"))
        invalid("SQL identifiers cannot use the generator's reserved __orm_ prefix", backend, std::string{path});
    const auto maximum = backend == db::BackendType::Postgres ? 63U : family(backend) ? 64U : name.size();
    if (name.size() > maximum)
    {
        invalid("SQL identifier exceeds the backend's supported identifier length", backend, std::string{path});
    }
}

auto appendToken(std::string& bytes, std::string_view value) -> void
{
    bytes += std::to_string(value.size()) + ":";
    bytes += value;
}

auto constraintName(std::string_view kind, std::string_view table, std::span<const std::string> columns,
                    std::string_view target = {}, std::span<const std::string> targetColumns = {}) -> std::string
{
    std::string bytes{"orm-cxx-constraint-v1:"};
    appendToken(bytes, kind);
    appendToken(bytes, table);
    appendToken(bytes, std::to_string(columns.size()));
    for (const auto& column : columns)
        appendToken(bytes, column);
    appendToken(bytes, target);
    appendToken(bytes, std::to_string(targetColumns.size()));
    for (const auto& column : targetColumns)
        appendToken(bytes, column);
    return "orm_" + std::string{kind} + "_" + detail::sha256(bytes).substr(0, 24);
}

auto tableOptions(db::BackendType backend) -> std::string
{
    if (!family(backend))
        return {};
    return "ENGINE=InnoDB DEFAULT CHARACTER SET utf8mb4 COLLATE " +
           std::string{backend == db::BackendType::Mysql ? "utf8mb4_0900_bin" : "utf8mb4_nopad_bin"};
}

auto supportsKey(std::span<const std::string> indexColumns, std::span<const std::string> keyColumns) -> bool
{
    return indexColumns.size() >= keyColumns.size() &&
           std::equal(keyColumns.begin(), keyColumns.end(), indexColumns.begin());
}

auto foreignKeyIndexes(PhysicalTable& table) -> void
{
    std::vector<const PhysicalForeignKey*> keys;
    keys.reserve(table.foreignKeys.size());
    for (const auto& key : table.foreignKeys)
        keys.push_back(&key);
    // A wider declared index can support a shorter foreign key. Stable ordering
    // keeps the physical schema independent of C++ member order.
    std::ranges::sort(keys,
                      [](const auto* left, const auto* right)
                      {
                          if (left->columns.size() != right->columns.size())
                              return left->columns.size() > right->columns.size();
                          return left->columns < right->columns;
                      });
    for (const auto* key : keys)
    {
        if (supportsKey(table.primaryKey, key->columns) ||
            std::ranges::any_of(table.indexes,
                                [&](const auto& index) { return supportsKey(index.columns, key->columns); }))
            continue;
        table.indexes.push_back(
            {.name = constraintName("fkidx", table.name, key->columns), .columns = key->columns, .unique = false});
    }
}

auto sqlType(db::BackendType backend, model::ColumnType type, std::optional<std::size_t> length) -> std::string
{
    requireBackend(backend);
    if (length && (type != model::ColumnType::String || *length == 0 || (family(backend) && *length > 16'383)))
        invalid("String length is invalid for this column and backend", backend);
    using enum model::ColumnType;
    if (family(backend))
    {
        switch (type)
        {
        case Bool:
            return "TINYINT";
        case Char:
            return "SMALLINT";
        case UnsignedChar:
            return "TINYINT UNSIGNED";
        case Short:
            return "SMALLINT";
        case UnsignedShort:
            return "SMALLINT UNSIGNED";
        case Int:
            return "INT";
        case UnsignedInt:
            return "INT UNSIGNED";
        case LongLong:
            return "BIGINT";
        case UnsignedLongLong:
            return "BIGINT UNSIGNED";
        case Float:
        case Double:
            return "DOUBLE";
        case String:
            return length ? std::format("VARCHAR({})", *length) : "LONGTEXT";
        case Date:
            return "DATE";
        case TimeOfDay:
            return "TIME(6)";
        case LocalDateTime:
            return "DATETIME(6)";
        case UtcTimestamp:
            return "TIMESTAMP(6)";
        case Uuid:
            if (backend == db::BackendType::Mariadb)
                return "UUID";
            break;
        }
    }
    else if (backend == db::BackendType::Sqlite)
    {
        switch (type)
        {
        case Bool:
            return "BOOLEAN";
        case Char:
        case UnsignedChar:
            return "TINYINT";
        case Short:
        case UnsignedShort:
            return "SMALLINT";
        case Int:
            return "INTEGER";
        case UnsignedInt:
        case UnsignedLongLong:
            return "UNSIGNED BIG INT";
        case LongLong:
            return "BIGINT";
        case Float:
        case Double:
            return "REAL";
        case String:
            return "TEXT";
        case Date:
        case TimeOfDay:
        case LocalDateTime:
        case UtcTimestamp:
        case Uuid:
            break;
        }
    }
    else
    {
        switch (type)
        {
        case Bool:
            return "BOOLEAN";
        case Char:
        case UnsignedChar:
        case Short:
            return "SMALLINT";
        case UnsignedShort:
        case Int:
            return "INTEGER";
        case UnsignedInt:
        case LongLong:
        case UnsignedLongLong:
            return "BIGINT";
        case Float:
        case Double:
            return "DOUBLE PRECISION";
        case String:
            return length ? std::format("VARCHAR({})", *length) : "TEXT";
        case Date:
            return "DATE";
        case TimeOfDay:
            return "TIME(6) WITHOUT TIME ZONE";
        case LocalDateTime:
            return "TIMESTAMP(6) WITHOUT TIME ZONE";
        case UtcTimestamp:
            return "TIMESTAMP(6) WITH TIME ZONE";
        case Uuid:
            return "UUID";
        }
    }
    invalid("The logical column type has no supported physical SQL type", backend);
}

auto keyColumns(model::ModelView model) -> std::vector<const model::ColumnView*>
{
    std::vector<const model::ColumnView*> result;
    for (const auto index : model->primaryKeyIndices)
    {
        if (index >= model->columns.size() || model->columns[index].kind != model::FieldKind::Scalar ||
            !model->columns[index].type)
            invalid("Primary-key indices must refer to scalar model columns");
        result.push_back(&model->columns[index]);
    }
    return result;
}

template <typename TypeResolver>
auto scalarColumn(std::string name, const model::ColumnView& column, bool nullable,
                  TypeResolver&& resolve) -> PhysicalColumn
{
    if (!column.type)
        invalid("Scalar column has no logical type");
    return PhysicalColumn{.name = std::move(name),
                          .logicalType = *column.type,
                          .sqlType = resolve(*column.type, column.maxStringLength),
                          .nullable = nullable,
                          .maxStringLength = column.maxStringLength,
                          .autoIncrement = false};
}

template <typename TypeResolver>
auto expandTable(model::ModelView model, TypeResolver&& resolve, bool mysqlPrimaryKey) -> PhysicalTable
{
    if (!model)
        invalid("Cannot capture an invalid model view");
    PhysicalTable table;
    table.name = std::string{model->tableName};
    for (const auto* column : keyColumns(model))
        table.primaryKey.emplace_back(column->name);
    if (!table.primaryKey.empty())
        table.primaryKeyName = mysqlPrimaryKey ? "PRIMARY" : constraintName("pk", table.name, table.primaryKey);
    for (const auto& column : model->columns)
    {
        if (column.kind == model::FieldKind::Scalar)
        {
            auto physical = scalarColumn(std::string{column.name}, column, !column.isNotNull, resolve);
            physical.autoIncrement = column.isAutoIncrement;
            table.columns.push_back(std::move(physical));
            continue;
        }
        const auto target = model.resolveTarget(column);
        if (!target)
            invalid("To-one relation target is not available in the schema");
        PhysicalForeignKey key;
        key.targetTable = std::string{target->tableName};
        const auto targetKey = keyColumns(target);
        if (targetKey.empty())
            invalid("To-one relation target must have a primary key");
        for (const auto* targetColumn : targetKey)
        {
            const auto name = std::string{column.name} + "_" + std::string{targetColumn->name};
            table.columns.push_back(scalarColumn(name, *targetColumn, !column.isNotNull, resolve));
            key.columns.push_back(name);
            key.targetColumns.emplace_back(targetColumn->name);
        }
        key.name = constraintName("fk", table.name, key.columns, key.targetTable, key.targetColumns);
        table.foreignKeys.push_back(std::move(key));
    }
    return table;
}

template <typename TypeResolver>
auto expandJunctions(model::ModelView model, TypeResolver&& resolve, bool mysqlPrimaryKey) -> std::vector<PhysicalTable>
{
    if (!model)
        invalid("Cannot capture junctions from an invalid model view");
    std::vector<PhysicalTable> result;
    for (const auto& relation : model->relations)
    {
        if (relation.kind != model::RelationKind::ManyToMany || !relation.junction.isConfigured() ||
            !relation.junction.owningSide)
            continue;
        const auto target = model.resolveTarget(relation);
        if (!target)
            invalid("Collection relation target is not available in the schema");
        const auto ownerKey = keyColumns(model);
        const auto targetKey = keyColumns(target);
        const auto& junction = relation.junction;
        if (ownerKey.empty() || targetKey.empty() || junction.ownerColumns.size() != ownerKey.size() ||
            junction.targetColumns.size() != targetKey.size())
            invalid("Junction columns must match both endpoint primary keys");
        PhysicalTable table;
        table.name = std::string{junction.tableName};
        const auto appendEndpoint = [&](model::ModelView endpoint, const auto& columns, const auto& physicalNames)
        {
            PhysicalForeignKey key;
            key.targetTable = std::string{endpoint->tableName};
            key.onDelete = "CASCADE";
            for (std::size_t index = 0; index < columns.size(); ++index)
            {
                const std::string name{physicalNames[index]};
                table.columns.push_back(scalarColumn(name, *columns[index], false, resolve));
                table.primaryKey.push_back(name);
                key.columns.push_back(name);
                key.targetColumns.emplace_back(columns[index]->name);
            }
            key.name = constraintName("fk", table.name, key.columns, key.targetTable, key.targetColumns);
            table.foreignKeys.push_back(std::move(key));
        };
        appendEndpoint(model, ownerKey, junction.ownerColumns);
        appendEndpoint(target, targetKey, junction.targetColumns);
        table.primaryKeyName = mysqlPrimaryKey ? "PRIMARY" : constraintName("pk", table.name, table.primaryKey);
        result.push_back(std::move(table));
    }
    return result;
}

template <typename Quote>
auto quotedList(std::span<const std::string> names, const Quote& quote) -> std::string
{
    std::string result;
    for (const auto& name : names)
    {
        if (!result.empty())
            result += ", ";
        result += quote(name);
    }
    return result;
}

auto validAction(std::string_view action) -> bool
{
    return action == "NO ACTION" || action == "RESTRICT" || action == "CASCADE" || action == "SET NULL";
}

template <typename Quote>
auto foreignKeySql(const PhysicalForeignKey& key, const Quote& quote) -> std::string
{
    if (key.columns.empty() || key.columns.size() != key.targetColumns.size() || !validAction(key.onDelete) ||
        !validAction(key.onUpdate))
        invalid("Invalid foreign-key shape or action");
    std::string result = "CONSTRAINT " + quote(key.name) + " FOREIGN KEY (" + quotedList(key.columns, quote) +
                         ") REFERENCES " + quote(key.targetTable) + " (" + quotedList(key.targetColumns, quote) + ")";
    if (key.onDelete != "NO ACTION")
        result += " ON DELETE " + key.onDelete;
    if (key.onUpdate != "NO ACTION")
        result += " ON UPDATE " + key.onUpdate;
    return result;
}

template <typename Quote, typename RenderColumn>
auto createTableSql(const PhysicalTable& table, std::string prefix, std::string suffix, bool includeForeignKeys,
                    bool inlineIndexes, const Quote& quote, const RenderColumn& columnSql) -> std::string
{
    if (table.columns.empty())
        invalid("Physical tables must contain at least one column");
    std::vector<std::string> definitions;
    bool inlinePrimary = false;
    for (const auto& column : table.columns)
    {
        if (column.autoIncrement &&
            (column.nullable || table.primaryKey.size() != 1 || table.primaryKey.front() != column.name ||
             column.logicalType != model::ColumnType::Int))
            invalid("Auto increment requires the table's single non-null integer primary key");
        const bool inlineColumn = column.autoIncrement;
        definitions.push_back(columnSql(column, inlineColumn, table.primaryKeyName));
        inlinePrimary = inlinePrimary || inlineColumn;
    }
    if (!table.primaryKey.empty() && !inlinePrimary)
    {
        const auto named =
            table.primaryKeyName == "PRIMARY" ? std::string{} : "CONSTRAINT " + quote(table.primaryKeyName) + " ";
        definitions.push_back(named + "PRIMARY KEY (" + quotedList(table.primaryKey, quote) + ")");
    }
    if (inlineIndexes)
        for (const auto& index : table.indexes)
            definitions.push_back(std::string{index.unique ? "UNIQUE INDEX " : "INDEX "} + quote(index.name) + " (" +
                                  quotedList(index.columns, quote) + ")");
    if (includeForeignKeys)
        for (const auto& key : table.foreignKeys)
            definitions.push_back(foreignKeySql(key, quote));
    std::string result = std::move(prefix) + "\n";
    for (std::size_t index = 0; index < definitions.size(); ++index)
    {
        result += "\t" + definitions[index];
        if (index + 1 != definitions.size())
            result += ",\n";
    }
    return result + suffix;
}

auto validSqlType(std::string_view type) -> bool
{
    std::size_t position = 0;
    const auto letter = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; };
    const auto digit = [](char c) { return c >= '0' && c <= '9'; };
    const auto spaces = [&]()
    {
        while (position < type.size() && type[position] == ' ')
            ++position;
    };
    const auto words = [&](std::string& name)
    {
        while (position < type.size() && letter(type[position]))
        {
            if (!name.empty())
                name += ' ';
            while (position < type.size() && (letter(type[position]) || digit(type[position])))
            {
                const auto c = type[position++];
                name += c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
            }
            const auto end = position;
            spaces();
            if (position == end)
                break;
        }
    };
    spaces();
    std::string name;
    words(name);
    if (name.empty())
        return false;
    if (position < type.size() && type[position] == '(')
    {
        ++position;
        const auto number = [&]()
        {
            spaces();
            const auto start = position;
            while (position < type.size() && digit(type[position]))
                ++position;
            spaces();
            return position > start && digit(type[start]);
        };
        if (!number())
            return false;
        if (position < type.size() && type[position] == ',')
        {
            ++position;
            if (!number())
                return false;
        }
        if (position == type.size() || type[position++] != ')')
            return false;
        const auto end = position;
        spaces();
        if (position < type.size())
        {
            if (position == end)
                return false;
            words(name);
        }
    }
    if (position != type.size())
        return false;
    constexpr std::array<std::string_view, 19> forbidden{
        "DEFAULT", "CHECK",   "CONSTRAINT",    "PRIMARY",        "KEY",  "FOREIGN", "REFERENCES",
        "UNIQUE",  "NOT",     "NULL",          "GENERATED",      "AS",   "STORED",  "VIRTUAL",
        "NOTNULL", "COLLATE", "AUTOINCREMENT", "AUTO_INCREMENT", "INDEX"};
    if (std::ranges::find(forbidden, name) != forbidden.end())
        return false;
    if (name.find(' ') == std::string::npos)
        return true;
    constexpr std::array<std::string_view, 15> multiword{"UNSIGNED BIG INT",
                                                         "DOUBLE PRECISION",
                                                         "BIG INT",
                                                         "CHARACTER VARYING",
                                                         "CHAR VARYING",
                                                         "VARYING CHARACTER",
                                                         "NATIONAL CHARACTER",
                                                         "NATIONAL CHAR",
                                                         "NATIONAL CHARACTER VARYING",
                                                         "NATIONAL CHAR VARYING",
                                                         "BIT VARYING",
                                                         "TIME WITHOUT TIME ZONE",
                                                         "TIME WITH TIME ZONE",
                                                         "TIMESTAMP WITHOUT TIME ZONE",
                                                         "TIMESTAMP WITH TIME ZONE"};
    if (std::ranges::find(multiword, name) != multiword.end())
        return true;
    constexpr std::array<std::string_view, 12> numeric{"TINYINT", "SMALLINT", "MEDIUMINT",        "INT",
                                                       "INTEGER", "BIGINT",   "DECIMAL",          "NUMERIC",
                                                       "FLOAT",   "DOUBLE",   "DOUBLE PRECISION", "REAL"};
    for (const std::string_view suffix : {" UNSIGNED", " SIGNED"})
        if (name.ends_with(suffix))
        {
            const auto base = std::string_view{name}.substr(0, name.size() - suffix.size());
            return std::ranges::find(numeric, base) != numeric.end();
        }
    return false;
}

auto validLogicalType(model::ColumnType type) -> bool
{
    using enum model::ColumnType;
    constexpr std::array values{Bool,        Char,     UnsignedChar,     Short,         UnsignedShort, Int,
                                UnsignedInt, LongLong, UnsignedLongLong, Float,         Double,        String,
                                Uuid,        Date,     TimeOfDay,        LocalDateTime, UtcTimestamp};
    return std::ranges::find(values, type) != values.end();
}

auto columnNamed(const PhysicalTable& table, std::string_view name) -> const PhysicalColumn*
{
    const auto found = std::ranges::find(table.columns, name, &PhysicalColumn::name);
    return found == table.columns.end() ? nullptr : &*found;
}

auto requireColumnList(const PhysicalTable& table, std::span<const std::string> names, db::BackendType backend,
                       bool allowEmpty = false) -> void
{
    std::set<std::string> seen;
    if (names.empty() && !allowEmpty)
        invalid("A key or index must have at least one column", backend, table.name);
    for (const auto& name : names)
        if (!columnNamed(table, name) || !seen.insert(name).second)
            invalid("A key or index contains an unknown or duplicate column", backend, table.name + "." + name);
}
} // namespace

namespace detail
{
auto quoteIdentifier(db::BackendType backend, std::string_view name) -> std::string
{
    requireBackend(backend);
    if (!validName(name))
        invalid("Cannot quote an empty or NUL-containing SQL identifier", backend);
    const char delimiter = family(backend) ? '`' : '"';
    std::string result(1, delimiter);
    for (const char c : name)
    {
        result.push_back(c);
        if (c == delimiter)
            result.push_back(c);
    }
    result.push_back(delimiter);
    return result;
}

auto renderColumn(db::BackendType backend, const PhysicalColumn& column, bool inlinePrimary,
                  std::string_view primaryName) -> std::string
{
    requireBackend(backend);
    if (!validSqlType(column.sqlType))
        invalid("Invalid frozen SQL column type", backend, column.name);
    const auto name = quoteIdentifier(backend, column.name);
    std::string result = name + " " + column.sqlType;
    if (column.autoIncrement)
    {
        if (column.nullable || column.logicalType != model::ColumnType::Int)
            invalid("Auto increment requires a non-null integer column", backend, column.name);
        if (backend == db::BackendType::Postgres)
            result += " GENERATED BY DEFAULT AS IDENTITY";
        else if (family(backend))
            result += " NOT NULL AUTO_INCREMENT";
        if (backend == db::BackendType::Sqlite && !inlinePrimary)
            invalid("SQLite auto increment must be rendered as an inline primary key", backend, column.name);
    }
    if (!column.nullable && !(column.autoIncrement && (family(backend) || backend == db::BackendType::Sqlite)))
        result += " NOT NULL";
    if (column.nullable && family(backend) && column.logicalType == model::ColumnType::UtcTimestamp)
        result += " NULL";
    if (inlinePrimary)
    {
        if (!primaryName.empty() && primaryName != "PRIMARY")
            result += " CONSTRAINT " + quoteIdentifier(backend, primaryName);
        result += " PRIMARY KEY";
        if (column.autoIncrement && backend == db::BackendType::Sqlite)
            result += " AUTOINCREMENT";
    }
    if (column.maxStringLength && backend == db::BackendType::Sqlite)
        result += std::format(" CHECK (length(replace({}, char(0), char(1))) <= {})", name, *column.maxStringLength);
    return result;
}

auto renderForeignKey(db::BackendType backend, const PhysicalForeignKey& key) -> std::string
{
    requireBackend(backend);
    return foreignKeySql(key, [backend](std::string_view name) { return quoteIdentifier(backend, name); });
}

auto renderCreateTable(db::BackendType backend, const PhysicalTable& table, bool includeForeignKeys,
                       bool ifNotExists) -> std::string
{
    requireBackend(backend);
    const auto prefix = "CREATE TABLE " + std::string{ifNotExists ? "IF NOT EXISTS " : ""} +
                        quoteIdentifier(backend, table.name) + " (";
    const auto suffix = "\n)" + (table.options.empty() ? "" : " " + table.options) + ";";
    return createTableSql(
        table, prefix, suffix, includeForeignKeys, family(backend),
        [backend](std::string_view name) { return quoteIdentifier(backend, name); },
        [backend](const PhysicalColumn& column, bool primary, std::string_view name)
        { return renderColumn(backend, column, primary, name); });
}

auto physicalTable(model::ModelView model, db::BackendType backend) -> PhysicalTable
{
    requireBackend(backend);
    auto table = expandTable(
        model, [backend](auto type, auto length) { return sqlType(backend, type, length); }, family(backend));
    table.options = tableOptions(backend);
    if (family(backend))
        foreignKeyIndexes(table);
    return table;
}

auto physicalJunctions(model::ModelView model, db::BackendType backend) -> std::vector<PhysicalTable>
{
    requireBackend(backend);
    auto tables = expandJunctions(
        model, [backend](auto type, auto length) { return sqlType(backend, type, length); }, family(backend));
    for (auto& table : tables)
    {
        table.options = tableOptions(backend);
        if (family(backend))
            foreignKeyIndexes(table);
    }
    return tables;
}

auto physicalTable(model::ModelView model, const db::SqlDialect& dialect) -> PhysicalTable
{
    const bool mysql = dialect.renderCreateTableSuffix().find("ENGINE=InnoDB") != std::string::npos;
    auto table =
        expandTable(model, [&dialect](auto type, auto length) { return dialect.toSqlType(type, length); }, mysql);
    if (mysql)
        foreignKeyIndexes(table);
    return table;
}

auto physicalJunctions(model::ModelView model, const db::SqlDialect& dialect) -> std::vector<PhysicalTable>
{
    const bool mysql = dialect.renderCreateTableSuffix().find("ENGINE=InnoDB") != std::string::npos;
    auto tables =
        expandJunctions(model, [&dialect](auto type, auto length) { return dialect.toSqlType(type, length); }, mysql);
    if (mysql)
        for (auto& table : tables)
            foreignKeyIndexes(table);
    return tables;
}

auto renderCreateTable(const db::SqlDialect& dialect, const PhysicalTable& table, bool includeForeignKeys,
                       bool ifNotExists) -> std::string
{
    return createTableSql(
        table, dialect.renderCreateTablePrefix(table.name, ifNotExists), dialect.renderCreateTableSuffix(),
        includeForeignKeys, dialect.renderCreateTableSuffix().find("ENGINE=InnoDB") != std::string::npos,
        [&dialect](std::string_view name) { return dialect.quoteIdentifier(name); },
        [&dialect](const PhysicalColumn& column, bool primary, std::string_view name)
        {
            if (!validSqlType(column.sqlType))
                invalid("Invalid frozen SQL column type", db::BackendType::Empty, column.name);
            if (column.autoIncrement)
            {
                auto result = dialect.renderAutoIncrementPrimaryKey(column.name);
                const auto position = result.find("PRIMARY KEY");
                if (position != std::string::npos && !name.empty() && name != "PRIMARY")
                    result.insert(position, "CONSTRAINT " + dialect.quoteIdentifier(name) + " ");
                return result;
            }
            static_cast<void>(primary);
            const bool nullableTimestamp = column.nullable && column.logicalType == model::ColumnType::UtcTimestamp &&
                                           dialect.renderCreateTableSuffix().find("ENGINE=InnoDB") != std::string::npos;
            return dialect.quoteIdentifier(column.name) + " " + column.sqlType +
                   (nullableTimestamp ? " NULL" :
                    column.nullable   ? "" :
                                        " NOT NULL") +
                   dialect.renderStringLengthConstraint(column.name, column.maxStringLength);
        });
}

auto canonicalize(SchemaSnapshot& snapshot) -> void
{
    for (auto& [backend, schema] : snapshot.backends)
    {
        static_cast<void>(backend);
        std::ranges::sort(schema.tables, {}, &PhysicalTable::name);
        for (auto& table : schema.tables)
        {
            std::ranges::sort(table.columns, {}, &PhysicalColumn::name);
            std::ranges::sort(table.foreignKeys, {}, &PhysicalForeignKey::name);
            std::ranges::sort(table.indexes, {}, &PhysicalIndex::name);
        }
    }
}

auto validateSnapshot(const SchemaSnapshot& snapshot) -> void
{
    if (snapshot.formatVersion != 1 || snapshot.backends.empty())
        invalid("Snapshot format must be version 1 and declare at least one backend");
    for (const auto& [backend, schema] : snapshot.backends)
    {
        requireBackend(backend);
        if (schema.backend != backend || schema.profile != profile(backend))
            invalid("Snapshot backend or DDL render profile is inconsistent", backend);
        std::set<std::string> tableNames;
        for (const auto& table : schema.tables)
        {
            requireName(table.name, backend, table.name);
            if (table.name == "_orm_migrations" || table.name == "_orm_migrations_incomplete" ||
                table.name.starts_with("sqlite_") || !tableNames.insert(table.name).second || table.columns.empty())
                invalid("Snapshot contains a duplicate, empty or reserved table", backend, table.name);
            if (table.options != tableOptions(backend))
                invalid("Snapshot table options do not match its frozen backend profile", backend, table.name);
            std::set<std::string> columnNames;
            std::set<std::string> constraintNames;
            for (const auto& column : table.columns)
            {
                const auto path = table.name + "." + column.name;
                requireName(column.name, backend, path);
                if (!columnNames.insert(column.name).second || !validLogicalType(column.logicalType) ||
                    !validSqlType(column.sqlType))
                    invalid("Snapshot column names and types must be valid and unique", backend, path);
                if (column.logicalType == model::ColumnType::Uuid || column.logicalType == model::ColumnType::Date ||
                    column.logicalType == model::ColumnType::TimeOfDay ||
                    column.logicalType == model::ColumnType::LocalDateTime ||
                    column.logicalType == model::ColumnType::UtcTimestamp)
                {
                    if (column.sqlType != sqlType(backend, column.logicalType, column.maxStringLength))
                        invalid("Temporal and UUID columns require their canonical native SQL type", backend, path);
                }
                if (column.maxStringLength &&
                    (column.logicalType != model::ColumnType::String || *column.maxStringLength == 0 ||
                     (family(backend) && *column.maxStringLength > 16'383)))
                    invalid("Snapshot contains an invalid bounded string", backend, path);
                if (column.autoIncrement && (column.nullable || column.logicalType != model::ColumnType::Int ||
                                             table.primaryKey.size() != 1 || table.primaryKey.front() != column.name))
                    invalid("Snapshot auto increment must be its single non-null integer primary key", backend, path);
            }
            requireColumnList(table, table.primaryKey, backend, true);
            for (const auto& name : table.primaryKey)
                if (columnNamed(table, name)->nullable)
                    invalid("Snapshot primary-key columns must be non-null", backend, table.name + "." + name);
            if (table.primaryKey.empty() != table.primaryKeyName.empty())
                invalid("Snapshot primary key and its name must be declared together", backend, table.name);
            if (!table.primaryKeyName.empty())
            {
                requireName(table.primaryKeyName, backend, table.name);
                if (family(backend) && table.primaryKeyName != "PRIMARY")
                    invalid("MySQL-family primary keys must use their physical name PRIMARY", backend, table.name);
                constraintNames.insert(table.primaryKeyName);
            }
            for (const auto& index : table.indexes)
            {
                requireName(index.name, backend, table.name);
                if (!constraintNames.insert(index.name).second)
                    invalid("Snapshot index names must be unique", backend, table.name);
                requireColumnList(table, index.columns, backend);
            }
            for (const auto& key : table.foreignKeys)
            {
                requireName(key.name, backend, table.name);
                if (!constraintNames.insert(key.name).second || !validAction(key.onDelete) ||
                    !validAction(key.onUpdate) || key.columns.size() != key.targetColumns.size())
                    invalid("Snapshot foreign-key names, actions and column counts must be valid", backend, table.name);
                requireColumnList(table, key.columns, backend);
                if (family(backend) && !supportsKey(table.primaryKey, key.columns) &&
                    !std::ranges::any_of(table.indexes,
                                         [&](const auto& index) { return supportsKey(index.columns, key.columns); }))
                    invalid("MySQL-family foreign keys require a declared supporting index", backend, table.name);
                const auto target = std::ranges::find(schema.tables, key.targetTable, &PhysicalTable::name);
                if (target == schema.tables.end())
                    invalid("Snapshot foreign key references an absent table", backend, table.name);
                requireColumnList(*target, key.targetColumns, backend);
                const bool targetIsUnique =
                    key.targetColumns == target->primaryKey ||
                    std::ranges::any_of(target->indexes, [&](const PhysicalIndex& index)
                                        { return index.unique && index.columns == key.targetColumns; });
                if (!targetIsUnique)
                    invalid("Snapshot foreign key must reference an ordered unique key", backend, table.name);
                for (std::size_t index = 0; index < key.columns.size(); ++index)
                {
                    const auto* local = columnNamed(table, key.columns[index]);
                    const auto* remote = columnNamed(*target, key.targetColumns[index]);
                    if (local->logicalType != remote->logicalType || local->sqlType != remote->sqlType ||
                        local->maxStringLength != remote->maxStringLength ||
                        ((key.onDelete == "SET NULL" || key.onUpdate == "SET NULL") && !local->nullable))
                        invalid("Snapshot foreign-key columns must have matching types and compatible nullability",
                                backend, table.name);
                }
            }
        }
    }
}
} // namespace detail

auto emptySnapshot(std::span<const db::BackendType> backends) -> SchemaSnapshot
{
    SchemaSnapshot result;
    for (const auto backend : backends)
    {
        requireBackend(backend);
        if (!result.backends
                 .emplace(backend, PhysicalSchema{.backend = backend, .profile = profile(backend), .tables = {}})
                 .second)
            invalid("Snapshot backend set contains duplicates", backend);
    }
    detail::validateSnapshot(result);
    return result;
}

auto capture(model::SchemaView models, std::span<const db::BackendType> backends) -> SchemaSnapshot
{
    auto result = emptySnapshot(backends);
    for (auto& [backend, schema] : result.backends)
    {
        for (std::size_t index = 0; index < models.models.size(); ++index)
        {
            const auto model = models.at(index);
            schema.tables.push_back(detail::physicalTable(model, backend));
            auto junctions = detail::physicalJunctions(model, backend);
            for (auto& table : junctions)
                schema.tables.push_back(std::move(table));
        }
    }
    detail::canonicalize(result);
    detail::validateSnapshot(result);
    return result;
}
} // namespace orm::migrations
