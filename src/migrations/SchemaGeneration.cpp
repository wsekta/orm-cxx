module;

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

module orm;

import :migrations_internal;

namespace
{
using namespace orm::migrations;
using Backend = orm::db::BackendType;

[[noreturn]] auto generationFailure(std::string code, std::string message, std::string path = {},
                                    Backend backend = Backend::Empty) -> void
{
    throw GenerationError{message, {{std::move(code), message, std::move(path), backend, true}}};
}

template <typename T>
auto findNamed(const std::vector<T>& values, std::string_view name) -> const T*
{
    const auto found = std::ranges::find(values, name, &T::name);
    return found == values.end() ? nullptr : &*found;
}

auto targetTable(std::string_view name, const GenerationDecisions& decisions) -> std::string
{
    for (const auto& rename : decisions.tableRenames)
        if (rename.from == name)
            return rename.to;
    return std::string{name};
}

auto targetColumn(std::string_view table, std::string_view name, const GenerationDecisions& decisions) -> std::string
{
    for (const auto& rename : decisions.columnRenames)
        if (rename.table == table && rename.from == name)
            return rename.to;
    return std::string{name};
}

auto sourceTable(std::string_view name, const GenerationDecisions& decisions) -> std::string
{
    for (const auto& rename : decisions.tableRenames)
        if (rename.to == name)
            return rename.from;
    return std::string{name};
}

auto sourceColumn(std::string_view table, std::string_view name, const GenerationDecisions& decisions) -> std::string
{
    for (const auto& rename : decisions.columnRenames)
        if (rename.table == table && rename.to == name)
            return rename.from;
    return std::string{name};
}

auto matchingSource(const PhysicalSchema& before, std::string_view target,
                    const GenerationDecisions& decisions) -> const PhysicalTable*
{
    const auto* source = findNamed(before.tables, sourceTable(target, decisions));
    return source && targetTable(source->name, decisions) == target ? source : nullptr;
}

auto matchingSourceColumn(const PhysicalTable& before, std::string_view table, std::string_view target,
                          const GenerationDecisions& decisions) -> const PhysicalColumn*
{
    const auto* source = findNamed(before.columns, sourceColumn(table, target, decisions));
    return source && targetColumn(table, source->name, decisions) == target ? source : nullptr;
}

auto quote(Backend backend, std::string_view name) -> std::string
{
    return orm::migrations::detail::quoteIdentifier(backend, name);
}

auto append(std::string& sql, std::string statement) -> void
{
    sql += statement;
    if (!statement.ends_with(';'))
        sql += ';';
    sql += '\n';
}

auto identifiers(Backend backend, const std::vector<std::string>& names) -> std::string
{
    std::string result;
    for (const auto& name : names)
    {
        if (!result.empty())
            result += ", ";
        result += quote(backend, name);
    }
    return result;
}

auto hexadecimal(std::string_view bytes) -> std::string
{
    constexpr std::string_view digits = "0123456789abcdef";
    std::string result;
    for (const char character : bytes)
    {
        const auto byte = static_cast<unsigned char>(character);
        result += digits[byte >> 4];
        result += digits[byte & 15];
    }
    return result;
}

auto unicodeLength(std::string_view value) -> std::size_t
{
    std::size_t count{};
    for (std::size_t index{}; index < value.size(); ++count)
    {
        const auto lead = static_cast<unsigned char>(value[index]);
        const std::size_t width = lead < 0x80                  ? 1 :
                                  lead >= 0xc2 && lead <= 0xdf ? 2 :
                                  lead >= 0xe0 && lead <= 0xef ? 3 :
                                  lead >= 0xf0 && lead <= 0xf4 ? 4 :
                                                                 0;
        if (width == 0 || index + width > value.size())
            generationFailure("invalid-literal", "String literals must contain valid UTF-8");
        std::uint32_t codepoint = lead & (width == 1 ? 0x7f : width == 2 ? 0x1f : width == 3 ? 0x0f : 0x07);
        for (std::size_t offset = 1; offset < width; ++offset)
        {
            const auto byte = static_cast<unsigned char>(value[index + offset]);
            if ((byte & 0xc0) != 0x80)
                generationFailure("invalid-literal", "String literals must contain valid UTF-8");
            codepoint = (codepoint << 6) | (byte & 0x3f);
        }
        if ((width == 2 && codepoint < 0x80) || (width == 3 && codepoint < 0x800) ||
            (width == 4 && codepoint < 0x10000) || (codepoint >= 0xd800 && codepoint <= 0xdfff) || codepoint > 0x10ffff)
            generationFailure("invalid-literal", "String literals must contain valid UTF-8");
        index += width;
    }
    return count;
}

auto integerLimits(orm::model::ColumnType type) -> std::pair<std::int64_t, std::uint64_t>
{
    using enum orm::model::ColumnType;
    switch (type)
    {
    case Bool:
        return {0, 1};
    case Char:
        return {std::numeric_limits<char>::lowest(), static_cast<std::uint64_t>(std::numeric_limits<char>::max())};
    case UnsignedChar:
        return {0, std::numeric_limits<unsigned char>::max()};
    case Short:
        return {std::numeric_limits<short>::lowest(), static_cast<std::uint64_t>(std::numeric_limits<short>::max())};
    case UnsignedShort:
        return {0, std::numeric_limits<unsigned short>::max()};
    case Int:
        return {std::numeric_limits<int>::lowest(), static_cast<std::uint64_t>(std::numeric_limits<int>::max())};
    case UnsignedInt:
        return {0, std::numeric_limits<unsigned int>::max()};
    case LongLong:
        return {std::numeric_limits<std::int64_t>::lowest(),
                static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())};
    case UnsignedLongLong:
        return {0, std::numeric_limits<std::uint64_t>::max()};
    default:
        generationFailure("invalid-literal", "Literal does not match the target logical type");
    }
}

auto floatingSql(double value) -> std::string
{
    char buffer[64];
    const auto result = std::to_chars(std::begin(buffer), std::end(buffer), value, std::chars_format::general,
                                      std::numeric_limits<double>::max_digits10);
    return std::string{buffer, result.ptr};
}

auto literalSql(const GenerationLiteral& value, const PhysicalColumn& column, Backend backend) -> std::string
{
    return std::visit(
        [&](const auto& item) -> std::string
        {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, std::monostate>)
            {
                if (!column.nullable)
                    generationFailure("invalid-literal", "NULL cannot fill a required column", column.name, backend);
                return "NULL";
            }
            else if constexpr (std::is_same_v<T, std::string>)
            {
                if (column.logicalType == orm::model::ColumnType::Uuid)
                {
                    const bool valid =
                        item.size() == 36 &&
                        std::ranges::all_of(std::views::iota(std::size_t{}, item.size()),
                                            [&](std::size_t index)
                                            {
                                                if (index == 8 || index == 13 || index == 18 || index == 23)
                                                    return item[index] == '-';
                                                const auto character = item[index];
                                                return (character >= '0' && character <= '9') ||
                                                       (character >= 'a' && character <= 'f') ||
                                                       (character >= 'A' && character <= 'F');
                                            });
                    if (backend != Backend::Postgres || !valid)
                        generationFailure("invalid-literal",
                                          "UUID literals require canonical UUID text and a PostgreSQL target",
                                          column.name, backend);
                    return "'" + item + "'::uuid";
                }
                if (column.logicalType != orm::model::ColumnType::String)
                    generationFailure("invalid-literal", "String literal requires a string target", column.name,
                                      backend);
                const auto length = unicodeLength(item);
                if (column.maxStringLength && length > *column.maxStringLength)
                    generationFailure("invalid-literal", "String literal exceeds the target bound", column.name,
                                      backend);
                if (backend == Backend::Sqlite)
                    return "CAST(X'" + hexadecimal(item) + "' AS TEXT)";
                if (backend == Backend::Mysql || backend == Backend::Mariadb)
                    return "CONVERT(X'" + hexadecimal(item) + "' USING utf8mb4)";
                if (item.find('\0') != std::string::npos)
                    generationFailure("invalid-literal", "PostgreSQL text cannot contain NUL", column.name, backend);
                return "convert_from(decode('" + hexadecimal(item) + "', 'hex'), 'UTF8')";
            }
            else if constexpr (std::is_same_v<T, double>)
            {
                if ((column.logicalType != orm::model::ColumnType::Float &&
                     column.logicalType != orm::model::ColumnType::Double) ||
                    !std::isfinite(item) ||
                    (column.logicalType == orm::model::ColumnType::Float &&
                     std::abs(item) > std::numeric_limits<float>::max()))
                    generationFailure("invalid-literal",
                                      "Floating literal requires a finite representable floating target", column.name,
                                      backend);
                const auto numeric = column.logicalType == orm::model::ColumnType::Float ?
                                         static_cast<double>(static_cast<float>(item)) :
                                         item;
                return floatingSql(numeric);
            }
            else
            {
                if (column.logicalType == orm::model::ColumnType::Float ||
                    column.logicalType == orm::model::ColumnType::Double)
                    return floatingSql(column.logicalType == orm::model::ColumnType::Float ?
                                           static_cast<double>(static_cast<float>(item)) :
                                           static_cast<double>(item));
                const auto [minimum, maximum] = integerLimits(column.logicalType);
                if constexpr (std::is_same_v<T, std::int64_t>)
                {
                    if (item < minimum || (item >= 0 && static_cast<std::uint64_t>(item) > maximum))
                        generationFailure("invalid-literal", "Integer literal is outside the target range", column.name,
                                          backend);
                }
                else if (static_cast<std::uint64_t>(item) > maximum)
                    generationFailure("invalid-literal", "Integer literal is outside the target range", column.name,
                                      backend);
                if constexpr (std::is_same_v<T, std::uint64_t>)
                    if ((backend == Backend::Sqlite || backend == Backend::Postgres) &&
                        item > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
                        generationFailure("invalid-literal", "Backend cannot represent this unsigned literal",
                                          column.name, backend);
                if (column.logicalType == orm::model::ColumnType::Bool && backend == Backend::Postgres)
                    return item ? "TRUE" : "FALSE";
                return std::to_string(item);
            }
        },
        value);
}

auto validateExpression(std::string_view expression, Backend backend) -> void
{
    if (expression.empty() || expression.find('\0') != std::string_view::npos)
        generationFailure("invalid-expression", "A transformation requires one nonempty SQL expression", {}, backend);
    char quoted{};
    int parentheses{};
    for (std::size_t index{}; index < expression.size(); ++index)
    {
        const auto character = expression[index];
        if (quoted != 0)
        {
            if (character == '\\' && backend != Backend::Sqlite && backend != Backend::Postgres && quoted != '`')
                ++index;
            else if (character == quoted)
            {
                if (index + 1 < expression.size() && expression[index + 1] == quoted)
                    ++index;
                else
                    quoted = 0;
            }
        }
        else if (character == '\'' || character == '"' || character == '`')
            quoted = character;
        else if (character == ';' || character == '#' || character == '$' || character == '\\' ||
                 (character == '-' && index + 1 < expression.size() && expression[index + 1] == '-') ||
                 (character == '/' && index + 1 < expression.size() && expression[index + 1] == '*'))
            generationFailure("invalid-expression",
                              "Transformation expressions cannot contain commands, comments, or dollar quoting", {},
                              backend);
        else if (character == '(')
            ++parentheses;
        else if (character == ')' && --parentheses < 0)
            generationFailure("invalid-expression", "Unbalanced transformation expression", {}, backend);
    }
    if (quoted != 0 || parentheses != 0)
        generationFailure("invalid-expression", "Unbalanced transformation expression", {}, backend);
    orm::migrations::detail::validateScript("SELECT " + std::string{expression} + ";", backend, 0);
}

auto findTransformation(const std::vector<ColumnTransformation>& values, std::string_view table,
                        std::string_view column) -> const ColumnTransformation*
{
    const auto found =
        std::ranges::find_if(values, [&](const auto& value) { return value.table == table && value.column == column; });
    return found == values.end() ? nullptr : &*found;
}

auto transformationSql(const ColumnTransformation& value, const PhysicalTable& source, const PhysicalColumn& target,
                       Backend backend) -> std::string
{
    const int count = static_cast<int>(value.constant.has_value()) + static_cast<int>(value.sourceColumn.has_value()) +
                      static_cast<int>(!value.expressions.empty());
    if (count != 1)
        generationFailure("invalid-transformation", "Choose exactly one literal, source column, or backend expression",
                          value.table + "/" + value.column, backend);
    if (value.constant)
        return literalSql(*value.constant, target, backend);
    if (value.sourceColumn)
    {
        const auto* original = findNamed(source.columns, *value.sourceColumn);
        if (!original)
            generationFailure("invalid-transformation", "Source column does not exist in the preceding snapshot",
                              *value.sourceColumn, backend);
        if (original->nullable && !target.nullable)
            generationFailure("invalid-transformation",
                              "A nullable source cannot fill a required target without an explicit expression",
                              *value.sourceColumn, backend);
        return quote(backend, *value.sourceColumn);
    }
    const auto expression = value.expressions.find(backend);
    if (expression == value.expressions.end())
        generationFailure("missing-expression", "Transformation has no expression for this backend",
                          value.table + "/" + value.column, backend);
    validateExpression(expression->second, backend);
    return expression->second;
}

auto validateTransformations(const PhysicalSchema& before, const PhysicalSchema& after,
                             const GenerationDecisions& decisions) -> void
{
    for (const auto* list : {&decisions.backfills, &decisions.conversions})
    {
        std::set<std::pair<std::string, std::string>> seen;
        for (const auto& transformation : *list)
        {
            const auto* old = matchingSource(before, transformation.table, decisions);
            const auto* next = findNamed(after.tables, transformation.table);
            const auto* column = next ? findNamed(next->columns, transformation.column) : nullptr;
            if (!seen.emplace(transformation.table, transformation.column).second || !old || !column)
                generationFailure("invalid-transformation",
                                  "Transformations must identify a unique existing table and target column",
                                  transformation.table + "/" + transformation.column, before.backend);
            if (list == &decisions.conversions && !matchingSourceColumn(*old, next->name, column->name, decisions))
                generationFailure("invalid-transformation", "Conversions require an existing source column",
                                  transformation.table + "/" + transformation.column, before.backend);
            (void)transformationSql(transformation, *old, *column, before.backend);
        }
    }
}

auto indexSql(Backend backend, std::string_view table, const PhysicalIndex& index) -> std::string
{
    return "CREATE " + std::string{index.unique ? "UNIQUE " : ""} + "INDEX " + quote(backend, index.name) + " ON " +
           quote(backend, table) + " (" + identifiers(backend, index.columns) + ");";
}

auto temporaryName(Version version, std::string_view direction, std::string_view source, std::string_view target,
                   std::string_view prefix = "__orm_rebuild_") -> std::string
{
    return std::string{prefix} +
           orm::migrations::detail::sha256(std::to_string(version) + ":" + std::string{direction} + ":" +
                                           std::to_string(source.size()) + ":" + std::string{source} + ":" +
                                           std::to_string(target.size()) + ":" + std::string{target})
               .substr(0, 16);
}

auto identity(const PhysicalTable& table) -> std::optional<std::string>
{
    for (const auto& column : table.columns)
        if (column.autoIncrement)
            return column.name;
    return std::nullopt;
}

auto changedTables(const SchemaDiff& diff, Backend backend) -> std::set<std::string>
{
    std::set<std::string> result;
    for (const auto& change : diff.changes)
        if (change.backend == backend)
            result.insert(change.kind == SchemaChangeKind::RenameTable ? change.otherName : change.table);
    return result;
}

auto nativeAffectedSources(const SchemaDiff& diff, const GenerationDecisions& decisions,
                           Backend backend) -> std::set<std::string>
{
    const auto& before = diff.before.backends.at(backend);
    const auto& after = diff.after.backends.at(backend);
    const auto changed = changedTables(diff, backend);
    std::set<std::string> affected;
    for (const auto& table : before.tables)
        if (changed.contains(targetTable(table.name, decisions)) ||
            !findNamed(after.tables, targetTable(table.name, decisions)))
            affected.insert(table.name);
    std::size_t previousSize{};
    do
    {
        previousSize = affected.size();
        for (const auto& table : before.tables)
            for (const auto& key : table.foreignKeys)
                if (affected.contains(key.targetTable))
                    affected.insert(table.name);
    } while (affected.size() != previousSize);
    return affected;
}

auto sqliteSql(Version version, std::string_view direction, const SchemaDiff& diff,
               const GenerationDecisions& decisions) -> std::pair<std::string, MigrationExecutionContext>
{
    const auto backend = Backend::Sqlite;
    const auto& before = diff.before.backends.at(backend);
    const auto& after = diff.after.backends.at(backend);
    const auto changed = changedTables(diff, backend);
    MigrationExecutionContext context;
    std::string sql;
    for (const auto& source : before.tables)
    {
        const auto* target = findNamed(after.tables, targetTable(source.name, decisions));
        if (target && !changed.contains(target->name))
            continue;
        context.mode = MigrationExecutionMode::SqliteRebuild;
        context.tables.push_back(source.name);
        if (!target)
            continue;
        const auto stage = temporaryName(version, direction, source.name, target->name);
        auto staged = *target;
        staged.name = stage;
        append(sql, orm::migrations::detail::renderCreateTable(backend, staged));
        std::vector<std::string> names;
        std::string values;
        for (const auto& column : target->columns)
        {
            names.push_back(column.name);
            if (!values.empty())
                values += ", ";
            const auto* old = matchingSourceColumn(source, target->name, column.name, decisions);
            const auto* conversion = findTransformation(decisions.conversions, target->name, column.name);
            const auto* backfill = findTransformation(decisions.backfills, target->name, column.name);
            auto expression = conversion ? transformationSql(*conversion, source, column, backend) :
                              old        ? quote(backend, old->name) :
                              backfill   ? transformationSql(*backfill, source, column, backend) :
                                           "NULL";
            if (old && old->nullable && !column.nullable && backfill && !conversion)
            {
                expression.insert(0, "COALESCE(");
                expression.append(", ");
                expression.append(transformationSql(*backfill, source, column, backend));
                expression.push_back(')');
            }
            values += expression;
        }
        append(sql, "INSERT INTO " + quote(backend, stage) + " (" + identifiers(backend, names) + ") SELECT " + values +
                        " FROM " + quote(backend, source.name));
        context.rebuildTables.push_back({.source = source.name,
                                         .target = target->name,
                                         .staging = stage,
                                         .sourceIdentity = identity(source),
                                         .targetIdentity = identity(*target)});
    }
    // Every source is copied before any source is removed, including mutually dependent tables.
    for (const auto& table : context.tables)
        append(sql, "DROP TABLE " + quote(backend, table));
    for (const auto& rebuild : context.rebuildTables)
    {
        append(sql, "ALTER TABLE " + quote(backend, rebuild.staging) + " RENAME TO " + quote(backend, rebuild.target));
        for (const auto& index : findNamed(after.tables, rebuild.target)->indexes)
            append(sql, indexSql(backend, rebuild.target, index));
    }
    for (const auto& table : after.tables)
    {
        const auto* old = matchingSource(before, table.name, decisions);
        if (old)
            continue;
        append(sql, orm::migrations::detail::renderCreateTable(backend, table));
        for (const auto& index : table.indexes)
            append(sql, indexSql(backend, table.name, index));
    }
    context.source = before;
    context.target = after;
    return {sql.empty() ? "SELECT 1;\n" : sql, std::move(context)};
}

struct NativePreparation
{
    std::map<std::string, std::string> stages;
    std::vector<std::string> identityIndexes;
    std::vector<std::string> remove;
    bool replacePrimary{};
};

auto prepareNativeTable(std::string& sql, Version version, std::string_view direction, const PhysicalTable& oldTable,
                        const PhysicalTable& table, const GenerationDecisions& decisions,
                        Backend backend) -> NativePreparation
{
    const bool mysql = backend == Backend::Mysql || backend == Backend::Mariadb;
    const auto& name = oldTable.name;
    NativePreparation prepared;
    auto& stages = prepared.stages;
    auto& identityIndexes = prepared.identityIndexes;
    auto& remove = prepared.remove;
    auto& replacePrimary = prepared.replacePrimary;
    replacePrimary = oldTable.primaryKeyName != table.primaryKeyName;
    auto mappedPrimary = oldTable.primaryKey;
    for (auto& column : mappedPrimary)
        column = targetColumn(table.name, column, decisions);
    replacePrimary = replacePrimary || mappedPrimary != table.primaryKey;
    for (const auto& old : oldTable.columns)
    {
        const auto* next = findNamed(table.columns, targetColumn(table.name, old.name, decisions));
        if (old.autoIncrement && next && findTransformation(decisions.conversions, table.name, next->name))
            generationFailure("identity-conversion",
                              "Identity conversions need a manual migration preserving its high-water mark",
                              name + "/" + old.name, backend);
        if (old.autoIncrement && (!next || !next->autoIncrement || (mysql && replacePrimary)))
        {
            if (mysql)
            {
                auto plain = old;
                plain.autoIncrement = false;
                append(sql, "ALTER TABLE " + quote(backend, name) + " MODIFY COLUMN " +
                                orm::migrations::detail::renderColumn(backend, plain));
            }
            else
            {
                append(sql, "ALTER TABLE " + quote(backend, name) + " ALTER COLUMN " + quote(backend, old.name) +
                                " DROP IDENTITY IF EXISTS");
                append(sql, "ALTER TABLE " + quote(backend, name) + " ALTER COLUMN " + quote(backend, old.name) +
                                " DROP DEFAULT");
            }
        }
    }
    for (const auto& old : oldTable.columns)
        if (!findNamed(table.columns, targetColumn(table.name, old.name, decisions)))
            remove.push_back(old.name);
    for (const auto& column : table.columns)
    {
        const auto* old = matchingSourceColumn(oldTable, table.name, column.name, decisions);
        const auto* conversion = findTransformation(decisions.conversions, table.name, column.name);
        const auto* backfill = findTransformation(decisions.backfills, table.name, column.name);
        const bool stagedBackfill = old && backfill && old->nullable && !column.nullable;
        if (!old || conversion || stagedBackfill)
        {
            const auto stage = temporaryName(version, direction, name, column.name, "__orm_column_");
            auto staged = column;
            staged.name = stage;
            staged.nullable = !column.autoIncrement;
            staged.autoIncrement = column.autoIncrement && !old;
            std::string add = "ALTER TABLE " + quote(backend, name) + " ADD COLUMN " +
                              orm::migrations::detail::renderColumn(backend, staged);
            if (mysql && staged.autoIncrement)
            {
                const auto index = temporaryName(version, direction, name, column.name, "__orm_identity_");
                add += ", ADD INDEX " + quote(backend, index) + " (" + quote(backend, stage) + ")";
                identityIndexes.push_back(index);
            }
            append(sql, std::move(add));
            const auto* fill = conversion ? conversion : backfill;
            if (fill)
            {
                auto expression = transformationSql(*fill, oldTable, column, backend);
                if (old && backfill && !conversion)
                {
                    auto coalesced = std::string{"COALESCE("};
                    coalesced.append(quote(backend, old->name));
                    coalesced.append(", ");
                    coalesced.append(expression);
                    coalesced.push_back(')');
                    expression = std::move(coalesced);
                }
                append(sql, "UPDATE " + quote(backend, name) + " SET " + quote(backend, stage) + " = " + expression);
            }
            stages[column.name] = stage;
            if (old)
            {
                remove.push_back(old->name);
                replacePrimary =
                    replacePrimary || std::ranges::find(oldTable.primaryKey, old->name) != oldTable.primaryKey.end();
            }
        }
    }
    return prepared;
}

auto nativeSql(Version version, std::string_view direction, const SchemaDiff& diff,
               const GenerationDecisions& decisions, Backend backend) -> std::string
{
    const bool mysql = backend == Backend::Mysql || backend == Backend::Mariadb;
    const auto& before = diff.before.backends.at(backend);
    const auto& after = diff.after.backends.at(backend);
    const auto changed = changedTables(diff, backend);
    const auto affectedSources = nativeAffectedSources(diff, decisions, backend);
    auto affectedTargets = changed;
    for (const auto& source : affectedSources)
        affectedTargets.insert(targetTable(source, decisions));
    std::string sql;
    for (const auto& table : before.tables)
        for (const auto& key : table.foreignKeys)
            if (affectedSources.contains(table.name))
                append(sql, "ALTER TABLE " + quote(backend, table.name) +
                                (mysql ? " DROP FOREIGN KEY " : " DROP CONSTRAINT ") + quote(backend, key.name));
    for (const auto& table : before.tables)
        if (affectedSources.contains(table.name))
        {
            for (const auto& index : table.indexes)
                append(sql,
                       "DROP INDEX " + quote(backend, index.name) + (mysql ? " ON " + quote(backend, table.name) : ""));
        }

    std::map<std::string, NativePreparation> preparations;
    for (const auto& source : before.tables)
    {
        const auto* target = findNamed(after.tables, targetTable(source.name, decisions));
        if (target && changed.contains(target->name))
            preparations.emplace(source.name,
                                 prepareNativeTable(sql, version, direction, source, *target, decisions, backend));
    }

    // Two-phase table renames also support explicit permutations of existing names.
    for (const auto& rename : decisions.tableRenames)
        append(sql, "ALTER TABLE " + quote(backend, rename.from) + " RENAME TO " +
                        quote(backend, temporaryName(version, direction, rename.from, rename.to, "__orm_rename_")));
    for (const auto& rename : decisions.tableRenames)
        append(sql, "ALTER TABLE " +
                        quote(backend, temporaryName(version, direction, rename.from, rename.to, "__orm_rename_")) +
                        " RENAME TO " + quote(backend, rename.to));

    for (const auto& oldTable : before.tables)
    {
        const auto name = targetTable(oldTable.name, decisions);
        const auto* table = findNamed(after.tables, name);
        if (!table)
        {
            append(sql, "DROP TABLE " + quote(backend, oldTable.name));
            continue;
        }
        if (!changed.contains(name))
            continue;
        auto& preparation = preparations.at(oldTable.name);
        const auto& stages = preparation.stages;
        const auto& identityIndexes = preparation.identityIndexes;
        const auto& remove = preparation.remove;
        const bool replacePrimary = preparation.replacePrimary;
        if (replacePrimary && !oldTable.primaryKey.empty())
        {
            append(sql,
                   "ALTER TABLE " + quote(backend, name) +
                       (mysql ? " DROP PRIMARY KEY" : " DROP CONSTRAINT " + quote(backend, oldTable.primaryKeyName)));
        }
        for (const auto& column : remove)
            append(sql, "ALTER TABLE " + quote(backend, name) + " DROP COLUMN " + quote(backend, column));
        for (const auto& old : oldTable.columns)
        {
            const auto target = targetColumn(name, old.name, decisions);
            if (target != old.name && findNamed(table->columns, target) && !stages.contains(target))
                append(sql,
                       "ALTER TABLE " + quote(backend, name) + " RENAME COLUMN " + quote(backend, old.name) + " TO " +
                           quote(backend, temporaryName(version, direction, name, old.name, "__orm_rename_column_")));
        }
        for (const auto& old : oldTable.columns)
        {
            const auto target = targetColumn(name, old.name, decisions);
            if (target != old.name && findNamed(table->columns, target) && !stages.contains(target))
                append(sql,
                       "ALTER TABLE " + quote(backend, name) + " RENAME COLUMN " +
                           quote(backend, temporaryName(version, direction, name, old.name, "__orm_rename_column_")) +
                           " TO " + quote(backend, target));
        }
        for (const auto& [column, stage] : stages)
            append(sql, "ALTER TABLE " + quote(backend, name) + " RENAME COLUMN " + quote(backend, stage) + " TO " +
                            quote(backend, column));
        for (const auto& column : table->columns)
        {
            const auto* old = matchingSourceColumn(oldTable, name, column.name, decisions);
            if (!old || stages.contains(column.name) || old->sqlType != column.sqlType ||
                old->nullable != column.nullable || old->maxStringLength != column.maxStringLength ||
                old->autoIncrement != column.autoIncrement || (mysql && replacePrimary && column.autoIncrement))
            {
                if (mysql)
                {
                    auto definition = column;
                    // AUTO_INCREMENT must retain an index through every ALTER. Restore it with the final PK below.
                    if (replacePrimary && column.autoIncrement)
                        definition.autoIncrement = false;
                    append(sql, "ALTER TABLE " + quote(backend, name) + " MODIFY COLUMN " +
                                    orm::migrations::detail::renderColumn(backend, definition));
                }
                else
                {
                    if (old && !stages.contains(column.name) && old->sqlType != column.sqlType)
                        append(sql, "ALTER TABLE " + quote(backend, name) + " ALTER COLUMN " +
                                        quote(backend, column.name) + " TYPE " + column.sqlType);
                    if (!column.nullable)
                        append(sql, "ALTER TABLE " + quote(backend, name) + " ALTER COLUMN " +
                                        quote(backend, column.name) + " SET NOT NULL");
                    else if (old && !old->nullable && !stages.contains(column.name))
                        append(sql, "ALTER TABLE " + quote(backend, name) + " ALTER COLUMN " +
                                        quote(backend, column.name) + " DROP NOT NULL");
                    if (column.autoIncrement && old && !old->autoIncrement)
                        append(sql, "ALTER TABLE " + quote(backend, name) + " ALTER COLUMN " +
                                        quote(backend, column.name) + " ADD GENERATED BY DEFAULT AS IDENTITY");
                }
            }
        }
        if (replacePrimary && !table->primaryKey.empty())
        {
            std::string primary =
                "ALTER TABLE " + quote(backend, name) + " ADD " +
                (mysql ? std::string{} : "CONSTRAINT " + quote(backend, table->primaryKeyName) + " ") +
                "PRIMARY KEY (" + identifiers(backend, table->primaryKey) + ")";
            if (mysql)
                for (const auto& column : table->columns)
                    if (column.autoIncrement)
                        primary += ", MODIFY COLUMN " + orm::migrations::detail::renderColumn(backend, column);
            append(sql, std::move(primary));
        }
        for (const auto& index : identityIndexes)
            append(sql, "DROP INDEX " + quote(backend, index) + " ON " + quote(backend, name));
        if (!mysql)
            for (const auto& column : table->columns)
            {
                const auto* old = matchingSourceColumn(oldTable, name, column.name, decisions);
                if (column.autoIncrement && (!old || !old->autoIncrement))
                {
                    const PhysicalColumn text{.logicalType = orm::model::ColumnType::String, .nullable = false};
                    append(sql, "SELECT setval(pg_get_serial_sequence(" +
                                    literalSql(std::string{quote(backend, name)}, text, backend) + ", " +
                                    literalSql(column.name, text, backend) + "), GREATEST(COALESCE(MAX(" +
                                    quote(backend, column.name) + "), 1), 1), COUNT(*) > 0) FROM " +
                                    quote(backend, name));
                }
            }
    }
    for (const auto& table : after.tables)
        if (!matchingSource(before, table.name, decisions))
            append(sql, orm::migrations::detail::renderCreateTable(backend, table, false));
    for (const auto& table : after.tables)
    {
        const bool existing = matchingSource(before, table.name, decisions) != nullptr;
        if (affectedTargets.contains(table.name) && (!mysql || existing))
            for (const auto& index : table.indexes)
                append(sql, indexSql(backend, table.name, index));
    }
    // Referenced unique indexes on every table must exist before any final foreign key is added.
    for (const auto& table : after.tables)
    {
        for (const auto& key : table.foreignKeys)
            if (affectedTargets.contains(table.name))
                append(sql, "ALTER TABLE " + quote(backend, table.name) + " ADD " +
                                orm::migrations::detail::renderForeignKey(backend, key));
    }
    return sql.empty() ? "SELECT 1;\n" : sql;
}

auto inverseDecisions(const GenerationDecisions& decisions) -> GenerationDecisions
{
    GenerationDecisions inverse;
    for (const auto& rename : decisions.tableRenames)
        inverse.tableRenames.push_back({rename.to, rename.from});
    for (const auto& rename : decisions.columnRenames)
        inverse.columnRenames.push_back({sourceTable(rename.table, decisions), rename.to, rename.from});
    inverse.allowDataLossUp = decisions.allowDataLossDown;
    inverse.backfills = decisions.downBackfills;
    inverse.conversions = decisions.downConversions;
    return inverse;
}

auto nativeContext(const SchemaDiff& diff, const GenerationDecisions& decisions,
                   Backend backend) -> MigrationExecutionContext
{
    MigrationExecutionContext context;
    const auto& before = diff.before.backends.at(backend);
    const auto& after = diff.after.backends.at(backend);
    context.source = before;
    context.target = after;
    const auto affected = nativeAffectedSources(diff, decisions, backend);
    context.tables.assign(affected.begin(), affected.end());
    return context;
}

auto resolved(const SchemaDiff& diff) -> bool
{
    return !std::ranges::any_of(diff.diagnostics, [](const auto& item) { return item.error; }) &&
           !std::ranges::any_of(diff.changes, [](const auto& item) { return item.unresolved; });
}

auto reverseReady(const SchemaDiff& inverse, const SchemaDiff& forward, const GenerationDecisions& decisions,
                  const GenerationDecisions& inverseOptions, Backend backend) -> bool
{
    if (std::ranges::any_of(inverse.diagnostics, [&](const auto& item)
                            { return item.error && (item.backend == backend || item.backend == Backend::Empty); }) ||
        std::ranges::any_of(inverse.changes,
                            [&](const auto& item) { return item.backend == backend && item.unresolved; }))
        return false;
    for (const auto& change : forward.changes)
    {
        if (change.backend != backend)
            continue;
        if (change.kind == SchemaChangeKind::DropTable)
            return false; // This API has no mapping for restoring rows of an entirely removed table.
        if (change.kind == SchemaChangeKind::DropColumn)
        {
            const auto* fill =
                findTransformation(inverseOptions.backfills, sourceTable(change.table, decisions), change.column);
            if (!fill || (!fill->constant && !fill->sourceColumn && !fill->expressions.contains(backend)))
                return false;
        }
    }
    return true;
}
} // namespace

namespace orm::migrations
{
auto generate(Version version, std::string name, const SchemaDiff& supplied,
              const GenerationDecisions& decisions) -> GeneratedMigration
{
    if (version <= 0 || name.empty() ||
        !std::ranges::all_of(name,
                             [](unsigned char character)
                             {
                                 return (character >= 'a' && character <= 'z') ||
                                        (character >= 'A' && character <= 'Z') ||
                                        (character >= '0' && character <= '9') || character == '_' || character == '-';
                             }))
        generationFailure("invalid-migration", "Migration needs a positive version and a portable name");
    auto diff = compare(supplied.before, supplied.after, decisions);
    if (!resolved(diff))
    {
        auto diagnostics = diff.diagnostics;
        for (const auto& change : diff.changes)
            if (change.unresolved)
                diagnostics.push_back({"unresolved-change", "Change requires explicit approval or transformation",
                                       change.id, change.backend, true});
        throw GenerationError{"Migration contains unresolved changes", std::move(diagnostics)};
    }
    if (diff.changes.empty())
        generationFailure("empty-diff", "There is no physical schema change to generate");
    auto inverseOptions = inverseDecisions(decisions);
    auto inverse = compare(diff.after, diff.before, inverseOptions);
    Migration migration{.version = version, .name = std::move(name), .before = diff.before, .after = diff.after};
    for (const auto& [backend, before] : diff.before.backends)
    {
        const auto& after = diff.after.backends.at(backend);
        validateTransformations(before, after, decisions);
        SqlMigration scripts;
        scripts.beforeHash = detail::snapshotHash(diff.before, backend);
        scripts.afterHash = detail::snapshotHash(diff.after, backend);
        if (backend == Backend::Sqlite)
        {
            auto [sql, context] = sqliteSql(version, "up", diff, decisions);
            scripts.up = std::move(sql);
            scripts.upContext = std::move(context);
        }
        else
        {
            scripts.up = nativeSql(version, "up", diff, decisions, backend);
            scripts.upContext = nativeContext(diff, decisions, backend);
        }
        if (reverseReady(inverse, diff, decisions, inverseOptions, backend))
        {
            validateTransformations(after, before, inverseOptions);
            if (backend == Backend::Sqlite)
            {
                auto [sql, context] = sqliteSql(version, "down", inverse, inverseOptions);
                scripts.down = std::move(sql);
                scripts.downContext = std::move(context);
            }
            else
            {
                scripts.down = nativeSql(version, "down", inverse, inverseOptions, backend);
                scripts.downContext = nativeContext(inverse, inverseOptions, backend);
            }
        }
        detail::validateScript(scripts.up, backend, version);
        if (scripts.down)
            detail::validateScript(*scripts.down, backend, version);
        migration.scripts.emplace(backend, std::move(scripts));
    }
    return {.migration = std::move(migration), .diff = std::move(diff)};
}
} // namespace orm::migrations
