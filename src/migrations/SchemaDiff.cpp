module;

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module orm;

import :migrations_internal;

namespace
{
using namespace orm::migrations;
using Backend = orm::db::BackendType;

template <typename T>
auto named(const std::vector<T>& values, std::string_view name) -> const T*
{
    const auto found = std::ranges::find(values, name, &T::name);
    return found == values.end() ? nullptr : &*found;
}

auto renamedTable(std::string_view source, const GenerationDecisions& decisions) -> std::string
{
    for (const auto& rename : decisions.tableRenames)
        if (rename.from == source)
            return rename.to;
    return std::string{source};
}

auto renamedColumn(std::string_view table, std::string_view source, const GenerationDecisions& decisions) -> std::string
{
    for (const auto& rename : decisions.columnRenames)
        if (rename.table == table && rename.from == source)
            return rename.to;
    return std::string{source};
}

auto transformation(const std::vector<ColumnTransformation>& values, std::string_view table, std::string_view column,
                    Backend backend, bool requiresExpression = false) -> bool
{
    return std::ranges::any_of(values,
                               [&](const auto& value)
                               {
                                   return value.table == table && value.column == column &&
                                          (value.expressions.contains(backend) ||
                                           (!requiresExpression && (value.constant || value.sourceColumn)));
                               });
}

auto integerRange(std::string_view type) -> std::pair<int, bool>
{
    const bool unsignedType = type.ends_with(" UNSIGNED");
    if (unsignedType)
        type.remove_suffix(9);
    if (type == "TINYINT")
        return {8, unsignedType};
    if (type == "SMALLINT")
        return {16, unsignedType};
    if (type == "INT" || type == "INTEGER")
        return {32, unsignedType};
    if (type == "BIGINT")
        return {64, unsignedType};
    return {0, false};
}

auto safeTypeChange(Backend backend, const PhysicalColumn& source, const PhysicalColumn& target) -> bool
{
    if (source.logicalType == orm::model::ColumnType::String && target.logicalType == orm::model::ColumnType::String)
    {
        const bool logicalWidening =
            !target.maxStringLength || (source.maxStringLength && *target.maxStringLength >= *source.maxStringLength);
        const auto canonicalNative = [backend](const PhysicalColumn& column)
        {
            if (backend == Backend::Sqlite)
                return column.sqlType == "TEXT";
            const auto expected = column.maxStringLength ? "VARCHAR(" + std::to_string(*column.maxStringLength) + ")" :
                                  backend == Backend::Postgres ? "TEXT" :
                                                                 "LONGTEXT";
            return column.sqlType == expected;
        };
        return logicalWidening &&
               (source.sqlType == target.sqlType || (canonicalNative(source) && canonicalNative(target)));
    }
    const auto logicalRange = [backend](orm::model::ColumnType type) -> std::pair<int, bool>
    {
        using enum orm::model::ColumnType;
        switch (type)
        {
        case Bool:
            return {1, true};
        case Char:
            return {8, false};
        case UnsignedChar:
            return {8, true};
        case Short:
            return {16, false};
        case UnsignedShort:
            return {16, true};
        case Int:
            return {32, false};
        case UnsignedInt:
            return {32, true};
        case LongLong:
            return {64, false};
        case UnsignedLongLong:
            return {backend == Backend::Sqlite || backend == Backend::Postgres ? 63 : 64, true};
        default:
            return {0, false};
        }
    };
    const auto [oldWidth, oldUnsigned] = logicalRange(source.logicalType);
    const auto [newWidth, newUnsigned] = logicalRange(target.logicalType);
    if (oldWidth != 0 || newWidth != 0)
    {
        if (oldWidth == 0 || newWidth == 0 ||
            !((oldUnsigned == newUnsigned && newWidth >= oldWidth) ||
              (oldUnsigned && !newUnsigned && newWidth > oldWidth)))
            return false;
    }
    else if (source.logicalType != target.logicalType && !(source.logicalType == orm::model::ColumnType::Float &&
                                                           target.logicalType == orm::model::ColumnType::Double))
        return false;
    if (source.sqlType == target.sqlType)
        return true;
    if (backend == Backend::Sqlite)
    {
        const auto integer = [](std::string_view type)
        {
            return type == "TINYINT" || type == "SMALLINT" || type == "INTEGER" || type == "BIGINT" ||
                   type == "UNSIGNED BIG INT";
        };
        return integer(source.sqlType) && integer(target.sqlType);
    }
    const auto [sourceBits, sourceUnsigned] = integerRange(source.sqlType);
    const auto [targetBits, targetUnsigned] = integerRange(target.sqlType);
    return sourceBits != 0 && targetBits != 0 &&
           ((sourceUnsigned == targetUnsigned && targetBits >= sourceBits) ||
            (sourceUnsigned && !targetUnsigned && targetBits > sourceBits));
}

auto physicalColumnEqual(const PhysicalColumn& left, const PhysicalColumn& right) -> bool
{
    return left.logicalType == right.logicalType && left.sqlType == right.sqlType && left.nullable == right.nullable &&
           left.maxStringLength == right.maxStringLength && left.autoIncrement == right.autoIncrement;
}

auto approved(const std::vector<std::string>& approvals, std::string_view id) -> bool
{
    return std::ranges::find(approvals, id) != approvals.end();
}

auto changeId(Backend backend, std::string_view kind, std::string_view table,
              std::string_view object = {}) -> std::string
{
    const auto component = [](std::string_view value)
    {
        std::string encoded;
        for (const auto character : value)
        {
            if (character == '%')
                encoded.append("%25");
            else if (character == '/')
                encoded.append("%2F");
            else
                encoded.push_back(character);
        }
        return encoded;
    };
    auto result =
        std::string{orm::migrations::detail::backendName(backend)} + "/" + std::string{kind} + "/" + component(table);
    if (!object.empty())
        result += "/" + component(object);
    return result;
}

auto addChange(SchemaDiff& diff, Backend backend, SchemaChangeKind kind, std::string_view label, std::string table,
               std::string object, std::string other, bool dataLoss, bool needsInput,
               const GenerationDecisions& decisions) -> void
{
    const auto id = changeId(backend, label, table, object);
    diff.changes.push_back({.kind = kind,
                            .backend = backend,
                            .id = id,
                            .table = std::move(table),
                            .column = std::move(object),
                            .otherName = std::move(other),
                            .description = id,
                            .dataLoss = dataLoss,
                            .unresolved = needsInput || (dataLoss && !approved(decisions.allowDataLossUp, id))});
}

auto problem(SchemaDiff& diff, std::string code, std::string message, std::string path, Backend backend) -> void
{
    diff.diagnostics.push_back({std::move(code), std::move(message), std::move(path), backend, true});
}

auto validateRenames(SchemaDiff& diff, const PhysicalSchema& before, const PhysicalSchema& after,
                     const GenerationDecisions& decisions) -> void
{
    std::set<std::string> sources;
    std::set<std::string> targets;
    for (const auto& rename : decisions.tableRenames)
    {
        if (rename.from == rename.to || !sources.insert(rename.from).second || !targets.insert(rename.to).second ||
            !named(before.tables, rename.from) || !named(after.tables, rename.to))
            problem(diff, "invalid-rename", "Table renames must identify distinct existing source and target tables",
                    rename.from, before.backend);
        if (named(before.tables, rename.to) &&
            !std::ranges::any_of(decisions.tableRenames, [&](const auto& item) { return item.from == rename.to; }))
            problem(diff, "rename-collision", "Table rename target is occupied by another source table", rename.to,
                    before.backend);
    }
    sources.clear();
    targets.clear();
    for (const auto& rename : decisions.columnRenames)
    {
        const auto sourceTableName = [&]
        {
            for (const auto& table : decisions.tableRenames)
                if (table.to == rename.table)
                    return table.from;
            return rename.table;
        }();
        const auto* source = named(before.tables, sourceTableName);
        const auto* target = named(after.tables, rename.table);
        if (rename.from == rename.to || !sources.insert(rename.table + "/" + rename.from).second ||
            !targets.insert(rename.table + "/" + rename.to).second || !source || !target ||
            !named(source->columns, rename.from) || !named(target->columns, rename.to))
            problem(diff, "invalid-rename",
                    "Column renames use the target table and existing source and target columns",
                    rename.table + "/" + rename.from, before.backend);
        if (source && named(source->columns, rename.to) &&
            !std::ranges::any_of(decisions.columnRenames, [&](const auto& item)
                                 { return item.table == rename.table && item.from == rename.to; }))
            problem(diff, "rename-collision", "Column rename target is occupied by another source column",
                    rename.table + "/" + rename.to, before.backend);
    }
}

auto compareTables(SchemaDiff& diff, const PhysicalSchema& before, const PhysicalTable& source,
                   const PhysicalTable& target, const GenerationDecisions& decisions) -> void
{
    const auto backend = before.backend;
    if (source.name != target.name)
        addChange(diff, backend, SchemaChangeKind::RenameTable, "rename-table", source.name, {}, target.name, false,
                  false, decisions);
    std::set<std::string> matched;
    for (const auto& column : source.columns)
    {
        const auto targetName = renamedColumn(target.name, column.name, decisions);
        const auto* targetColumn = named(target.columns, targetName);
        if (!targetColumn)
        {
            addChange(diff, backend, SchemaChangeKind::DropColumn, "drop-column", target.name, column.name, {}, true,
                      false, decisions);
            continue;
        }
        matched.insert(targetName);
        if (column.name != targetName)
            addChange(diff, backend, SchemaChangeKind::RenameColumn, "rename-column", target.name, column.name,
                      targetName, false, false, decisions);
        if (!physicalColumnEqual(column, *targetColumn))
        {
            const bool conversion = !safeTypeChange(backend, column, *targetColumn);
            const bool identity = column.autoIncrement != targetColumn->autoIncrement;
            const bool required = column.nullable && !targetColumn->nullable;
            const bool timezoneConversion = (column.logicalType == orm::model::ColumnType::LocalDateTime &&
                                             targetColumn->logicalType == orm::model::ColumnType::UtcTimestamp) ||
                                            (column.logicalType == orm::model::ColumnType::UtcTimestamp &&
                                             targetColumn->logicalType == orm::model::ColumnType::LocalDateTime);
            const bool missingConversion = conversion && !transformation(decisions.conversions, target.name, targetName,
                                                                         backend, timezoneConversion);
            const bool missingBackfill = required &&
                                         !transformation(decisions.backfills, target.name, targetName, backend) &&
                                         !transformation(decisions.conversions, target.name, targetName, backend);
            addChange(diff, backend, SchemaChangeKind::AlterColumn, "alter-column", target.name, targetName, {},
                      conversion || identity, missingConversion || missingBackfill, decisions);
        }
    }
    for (const auto& column : target.columns)
        if (!matched.contains(column.name))
            addChange(diff, backend, SchemaChangeKind::AddColumn, "add-column", target.name, column.name, {}, false,
                      !column.nullable && !column.autoIncrement &&
                          !transformation(decisions.backfills, target.name, column.name, backend),
                      decisions);

    auto mappedPrimary = source.primaryKey;
    for (auto& column : mappedPrimary)
        column = renamedColumn(target.name, column, decisions);
    if (mappedPrimary != target.primaryKey || source.primaryKeyName != target.primaryKeyName)
        addChange(diff, backend, SchemaChangeKind::AlterPrimaryKey, "alter-primary-key", target.name, {}, {},
                  mappedPrimary != target.primaryKey, false, decisions);
    for (const auto& key : source.foreignKeys)
    {
        auto mapped = key;
        for (auto& column : mapped.columns)
            column = renamedColumn(target.name, column, decisions);
        mapped.targetTable = renamedTable(key.targetTable, decisions);
        for (auto& column : mapped.targetColumns)
            column = renamedColumn(mapped.targetTable, column, decisions);
        if (!std::ranges::any_of(target.foreignKeys, [&](const auto& value) { return value == mapped; }))
            addChange(diff, backend, SchemaChangeKind::DropForeignKey, "drop-foreign-key", target.name, key.name, {},
                      false, false, decisions);
    }
    for (const auto& key : target.foreignKeys)
    {
        const auto* old = named(source.foreignKeys, key.name);
        auto mapped = old ? *old : PhysicalForeignKey{};
        if (old)
        {
            for (auto& column : mapped.columns)
                column = renamedColumn(target.name, column, decisions);
            mapped.targetTable = renamedTable(mapped.targetTable, decisions);
            for (auto& column : mapped.targetColumns)
                column = renamedColumn(mapped.targetTable, column, decisions);
        }
        if (!old || mapped != key)
            addChange(diff, backend, SchemaChangeKind::AddForeignKey, "add-foreign-key", target.name, key.name, {},
                      false, false, decisions);
    }
    for (const auto& index : source.indexes)
    {
        auto mapped = index;
        for (auto& column : mapped.columns)
            column = renamedColumn(target.name, column, decisions);
        const auto* next = named(target.indexes, index.name);
        if (!next || mapped != *next)
            addChange(diff, backend, SchemaChangeKind::DropIndex, "drop-index", target.name, index.name, {}, false,
                      false, decisions);
    }
    for (const auto& index : target.indexes)
    {
        const auto* old = named(source.indexes, index.name);
        auto mapped = old ? *old : PhysicalIndex{};
        for (auto& column : mapped.columns)
            column = renamedColumn(target.name, column, decisions);
        if (!old || mapped != index)
            addChange(diff, backend, SchemaChangeKind::AddIndex, "add-index", target.name, index.name, {}, false, false,
                      decisions);
    }
}
} // namespace

namespace orm::migrations
{
auto compare(const SchemaSnapshot& before, const SchemaSnapshot& after,
             const GenerationDecisions& decisions) -> SchemaDiff
{
    SchemaDiff result{.before = before, .after = after};
    detail::canonicalize(result.before);
    detail::canonicalize(result.after);
    detail::validateSnapshot(result.before);
    detail::validateSnapshot(result.after);
    if (result.before.backends.size() != result.after.backends.size())
        problem(result, "backend-profile", "Snapshots must contain the same backend profiles", {}, Backend::Empty);
    for (const auto& [backend, source] : result.before.backends)
    {
        const auto found = result.after.backends.find(backend);
        if (found == result.after.backends.end() || source.profile != found->second.profile)
        {
            problem(result, "backend-profile", "Backend profiles cannot change within a migration", {}, backend);
            continue;
        }
        const auto& target = found->second;
        validateRenames(result, source, target, decisions);
        std::set<std::string> matched;
        for (const auto& table : source.tables)
        {
            const auto targetName = renamedTable(table.name, decisions);
            const auto* next = named(target.tables, targetName);
            if (!next)
                addChange(result, backend, SchemaChangeKind::DropTable, "drop-table", table.name, {}, {}, true, false,
                          decisions);
            else if (!matched.insert(targetName).second)
                problem(result, "rename-collision", "Two source tables map to the same target", targetName, backend);
            else
                compareTables(result, source, table, *next, decisions);
        }
        for (const auto& table : target.tables)
            if (!matched.contains(table.name))
                addChange(result, backend, SchemaChangeKind::CreateTable, "create-table", table.name, {}, {}, false,
                          false, decisions);
    }
    for (const auto& approval : decisions.allowDataLossUp)
        if (!std::ranges::any_of(result.changes,
                                 [&](const auto& change) { return change.id == approval && change.dataLoss; }))
            problem(result, "unknown-approval", "Data-loss approval does not identify a risky change", approval,
                    Backend::Empty);
    std::ranges::sort(result.changes, {}, &SchemaChange::id);
    return result;
}
} // namespace orm::migrations
