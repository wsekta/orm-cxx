module;

#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

module orm;

import :migrations_internal;
import :value_codec;

namespace orm::migrations::detail
{
namespace
{
using Json = nlohmann::json;

auto strictJson(std::string_view text) -> Json
{
    std::map<int, std::set<std::string>> keys;
    return Json::parse(text,
                       [&](int depth, Json::parse_event_t event, Json& value)
                       {
                           if (event == Json::parse_event_t::object_start)
                               keys[depth + 1].clear();
                           else if (event == Json::parse_event_t::object_end)
                               keys.erase(depth + 1);
                           else if (event == Json::parse_event_t::key &&
                                    !keys[depth].insert(value.get<std::string>()).second)
                           {
                               throw MigrationError{ErrorCode::InvalidCatalog, "Duplicate JSON object field"};
                           }
                           return true;
                       });
}

auto backendFromName(std::string_view name) -> db::BackendType
{
    for (const auto backend : generationBackends)
    {
        if (backendName(backend) == name)
        {
            return backend;
        }
    }
    throw MigrationError{ErrorCode::InvalidCatalog, "Unknown backend in migration metadata"};
}

auto requireKeys(const Json& value, std::initializer_list<std::string_view> keys) -> void
{
    if (!value.is_object() || value.size() != keys.size())
    {
        throw MigrationError{ErrorCode::InvalidCatalog, "Invalid migration metadata fields"};
    }
    for (const auto key : keys)
    {
        if (!value.contains(std::string{key}))
        {
            throw MigrationError{ErrorCode::InvalidCatalog, "Missing migration metadata field"};
        }
    }
}

template <typename T>
auto optionalValue(const Json& value) -> std::optional<T>
{
    return value.is_null() ? std::nullopt : std::optional<T>{value.get<T>()};
}

auto encodeSchema(const PhysicalSchema& schema) -> Json
{
    Json tables = Json::array();
    for (const auto& table : schema.tables)
    {
        Json columns = Json::array();
        for (const auto& column : table.columns)
        {
            columns.push_back({{"name", column.name},
                               {"logicalType", static_cast<int>(column.logicalType)},
                               {"sqlType", column.sqlType},
                               {"nullable", column.nullable},
                               {"maxStringLength", column.maxStringLength ? Json(*column.maxStringLength) : Json{}},
                               {"autoIncrement", column.autoIncrement}});
        }
        Json keys = Json::array();
        for (const auto& key : table.foreignKeys)
        {
            keys.push_back({{"name", key.name},
                            {"columns", key.columns},
                            {"targetTable", key.targetTable},
                            {"targetColumns", key.targetColumns},
                            {"onDelete", key.onDelete},
                            {"onUpdate", key.onUpdate}});
        }
        Json indexes = Json::array();
        for (const auto& index : table.indexes)
        {
            indexes.push_back({{"name", index.name}, {"columns", index.columns}, {"unique", index.unique}});
        }
        tables.push_back({{"name", table.name},
                          {"columns", columns},
                          {"primaryKey", table.primaryKey},
                          {"primaryKeyName", table.primaryKeyName},
                          {"foreignKeys", keys},
                          {"indexes", indexes},
                          {"options", table.options}});
    }
    return {{"backend", backendName(schema.backend)}, {"profile", schema.profile}, {"tables", tables}};
}

auto decodeSchema(const Json& value) -> PhysicalSchema
{
    requireKeys(value, {"backend", "profile", "tables"});
    PhysicalSchema schema;
    schema.backend = backendFromName(value.at("backend").get<std::string>());
    schema.profile = value.at("profile").get<std::string>();
    if (!value.at("tables").is_array())
    {
        throw MigrationError{ErrorCode::InvalidCatalog, "Snapshot tables must be an array"};
    }
    for (const auto& item : value.at("tables"))
    {
        requireKeys(item, {"name", "columns", "primaryKey", "primaryKeyName", "foreignKeys", "indexes", "options"});
        PhysicalTable table;
        table.name = item.at("name").get<std::string>();
        table.primaryKey = item.at("primaryKey").get<std::vector<std::string>>();
        table.primaryKeyName = item.at("primaryKeyName").get<std::string>();
        table.options = item.at("options").get<std::string>();
        if (!item.at("columns").is_array() || !item.at("foreignKeys").is_array() || !item.at("indexes").is_array())
        {
            throw MigrationError{ErrorCode::InvalidCatalog, "Snapshot column, key and index fields must be arrays"};
        }
        for (const auto& column : item.at("columns"))
        {
            requireKeys(column, {"name", "logicalType", "sqlType", "nullable", "maxStringLength", "autoIncrement"});
            if (!column.at("logicalType").is_number_integer() ||
                column.at("logicalType") < static_cast<int>(model::ColumnType::Bool) ||
                column.at("logicalType") > static_cast<int>(model::ColumnType::UtcTimestamp) ||
                (!column.at("maxStringLength").is_null() &&
                 (!column.at("maxStringLength").is_number_integer() || column.at("maxStringLength") < 0)))
            {
                throw MigrationError{ErrorCode::InvalidCatalog, "Column type and length must be valid integers"};
            }
            const auto type = column.at("logicalType").get<int>();
            table.columns.push_back(
                PhysicalColumn{.name = column.at("name").get<std::string>(),
                               .logicalType = static_cast<model::ColumnType>(type),
                               .sqlType = column.at("sqlType").get<std::string>(),
                               .nullable = column.at("nullable").get<bool>(),
                               .maxStringLength = optionalValue<std::size_t>(column.at("maxStringLength")),
                               .autoIncrement = column.at("autoIncrement").get<bool>()});
        }
        for (const auto& key : item.at("foreignKeys"))
        {
            requireKeys(key, {"name", "columns", "targetTable", "targetColumns", "onDelete", "onUpdate"});
            table.foreignKeys.push_back(
                PhysicalForeignKey{.name = key.at("name").get<std::string>(),
                                   .columns = key.at("columns").get<std::vector<std::string>>(),
                                   .targetTable = key.at("targetTable").get<std::string>(),
                                   .targetColumns = key.at("targetColumns").get<std::vector<std::string>>(),
                                   .onDelete = key.at("onDelete").get<std::string>(),
                                   .onUpdate = key.at("onUpdate").get<std::string>()});
        }
        for (const auto& index : item.at("indexes"))
        {
            requireKeys(index, {"name", "columns", "unique"});
            table.indexes.push_back(PhysicalIndex{.name = index.at("name").get<std::string>(),
                                                  .columns = index.at("columns").get<std::vector<std::string>>(),
                                                  .unique = index.at("unique").get<bool>()});
        }
        schema.tables.push_back(std::move(table));
    }
    return schema;
}

auto encodeSnapshot(const SchemaSnapshot& snapshot) -> Json
{
    Json backends = Json::object();
    for (const auto& [backend, schema] : snapshot.backends)
    {
        backends[std::string{backendName(backend)}] = encodeSchema(schema);
    }
    return {{"formatVersion", snapshot.formatVersion}, {"backends", backends}};
}

auto transform(const Json& value) -> ColumnTransformation
{
    if (!value.is_object() || !value.contains("table") || !value.contains("column"))
    {
        throw MigrationError{ErrorCode::InvalidCatalog, "A transformation requires table and column"};
    }
    ColumnTransformation result;
    result.table = value.at("table").get<std::string>();
    result.column = value.at("column").get<std::string>();
    std::size_t sources{};
    for (const auto& [key, item] : value.items())
    {
        if (key == "constant")
        {
            ++sources;
            if (item.is_null())
                result.constant = GenerationLiteral{std::monostate{}};
            else if (item.is_boolean())
                result.constant = GenerationLiteral{item.get<bool>()};
            else if (item.is_number_unsigned())
                result.constant = GenerationLiteral{item.get<std::uint64_t>()};
            else if (item.is_number_integer())
                result.constant = GenerationLiteral{item.get<std::int64_t>()};
            else if (item.is_number_float())
                result.constant = GenerationLiteral{item.get<double>()};
            else if (item.is_string())
                result.constant = GenerationLiteral{item.get<std::string>()};
            else if (item.is_object())
            {
                requireKeys(item, {"type", "value"});
                const auto type = item.at("type").get<std::string>();
                const auto text = item.at("value").get<std::string>();
                if (type == "date")
                    result.constant = orm::detail::values::parseDate(text);
                else if (type == "time_of_day")
                    result.constant = orm::detail::values::parseTimeOfDay(text);
                else if (type == "local_date_time")
                    result.constant = orm::detail::values::parseLocalDateTime(text);
                else if (type == "utc_timestamp")
                    result.constant = orm::detail::values::parseUtcTimestamp(text);
                else if (type == "uuid")
                    result.constant = orm::Uuid::parse(text);
                else
                    throw MigrationError{ErrorCode::InvalidCatalog, "Unknown typed backfill constant"};
            }
            else
                throw MigrationError{ErrorCode::InvalidCatalog, "A backfill constant must be a scalar or typed value"};
        }
        else if (key == "sourceColumn")
        {
            ++sources;
            result.sourceColumn = item.get<std::string>();
        }
        else if (key == "expressions")
        {
            ++sources;
            if (!item.is_object() || item.empty())
            {
                throw MigrationError{ErrorCode::InvalidCatalog, "Expressions must map backends to SQL expressions"};
            }
            for (const auto& [name, expression] : item.items())
            {
                result.expressions.emplace(backendFromName(name), expression.get<std::string>());
            }
        }
        else if (key != "table" && key != "column")
        {
            throw MigrationError{ErrorCode::InvalidCatalog, "Unknown transformation field"};
        }
    }
    if (sources != 1 || result.table.empty() || result.column.empty())
    {
        throw MigrationError{ErrorCode::InvalidCatalog, "A transformation requires exactly one value source"};
    }
    return result;
}

template <typename Function>
auto parseMetadata(std::string_view text, Function&& function)
{
    try
    {
        return function(strictJson(text));
    }
    catch (const Json::exception&)
    {
        throw MigrationError{ErrorCode::InvalidCatalog, "Invalid migration JSON metadata"};
    }
    catch (const GenerationError&)
    {
        throw MigrationError{ErrorCode::InvalidCatalog, "Invalid physical schema in migration metadata"};
    }
    catch (const std::invalid_argument&)
    {
        throw MigrationError{ErrorCode::InvalidCatalog, "Invalid typed migration value"};
    }
}
} // namespace

auto snapshotJson(const SchemaSnapshot& snapshot) -> std::string
{
    auto canonical = snapshot;
    canonicalize(canonical);
    validateSnapshot(canonical);
    return encodeSnapshot(canonical).dump(2) + '\n';
}

auto snapshotFromJson(std::string_view text) -> SchemaSnapshot
{
    return parseMetadata(text,
                         [](const Json& value)
                         {
                             requireKeys(value, {"formatVersion", "backends"});
                             if (!value.at("formatVersion").is_number_integer() || value.at("formatVersion") != 1)
                             {
                                 throw MigrationError{ErrorCode::InvalidCatalog, "Unsupported snapshot format version"};
                             }
                             SchemaSnapshot result;
                             result.formatVersion = value.at("formatVersion").get<int>();
                             if (!value.at("backends").is_object())
                             {
                                 throw MigrationError{ErrorCode::InvalidCatalog, "Snapshot backends must be an object"};
                             }
                             for (const auto& [name, schema] : value.at("backends").items())
                             {
                                 const auto backend = backendFromName(name);
                                 auto physical = decodeSchema(schema);
                                 if (physical.backend != backend)
                                 {
                                     throw MigrationError{ErrorCode::InvalidCatalog, "Snapshot backend names disagree"};
                                 }
                                 result.backends.emplace(backend, std::move(physical));
                             }
                             canonicalize(result);
                             validateSnapshot(result);
                             return result;
                         });
}

auto snapshotHash(const SchemaSnapshot& snapshot, db::BackendType backend) -> std::string
{
    SchemaSnapshot selected;
    selected.formatVersion = snapshot.formatVersion;
    selected.backends.emplace(backend, snapshot.backends.at(backend));
    return sha256(snapshotJson(selected));
}

auto contextJson(const MigrationExecutionContext& context) -> std::string
{
    Json rebuild = Json::array();
    for (const auto& table : context.rebuildTables)
    {
        rebuild.push_back({{"source", table.source},
                           {"target", table.target},
                           {"staging", table.staging},
                           {"sourceIdentity", table.sourceIdentity ? Json(*table.sourceIdentity) : Json{}},
                           {"targetIdentity", table.targetIdentity ? Json(*table.targetIdentity) : Json{}}});
    }
    return Json{{"mode", context.mode == MigrationExecutionMode::Normal ? "normal" : "sqlite-rebuild"},
                {"tables", context.tables},
                {"rebuildTables", rebuild},
                {"source", context.source ? encodeSchema(*context.source) : Json{}},
                {"target", context.target ? encodeSchema(*context.target) : Json{}}}
               .dump(2) +
           '\n';
}

auto contextFromJson(std::string_view text) -> MigrationExecutionContext
{
    return parseMetadata(
        text,
        [](const Json& value)
        {
            requireKeys(value, {"mode", "tables", "rebuildTables", "source", "target"});
            MigrationExecutionContext result;
            const auto mode = value.at("mode").get<std::string>();
            if (mode != "normal" && mode != "sqlite-rebuild")
            {
                throw MigrationError{ErrorCode::InvalidCatalog, "Unknown migration execution mode"};
            }
            result.mode = mode == "normal" ? MigrationExecutionMode::Normal : MigrationExecutionMode::SqliteRebuild;
            result.tables = value.at("tables").get<std::vector<std::string>>();
            if (!value.at("rebuildTables").is_array())
            {
                throw MigrationError{ErrorCode::InvalidCatalog, "Rebuild tables must be an array"};
            }
            for (const auto& table : value.at("rebuildTables"))
            {
                requireKeys(table, {"source", "target", "staging", "sourceIdentity", "targetIdentity"});
                result.rebuildTables.push_back(
                    RebuildTable{.source = table.at("source").get<std::string>(),
                                 .target = table.at("target").get<std::string>(),
                                 .staging = table.at("staging").get<std::string>(),
                                 .sourceIdentity = optionalValue<std::string>(table.at("sourceIdentity")),
                                 .targetIdentity = optionalValue<std::string>(table.at("targetIdentity"))});
            }
            if (!value.at("source").is_null())
                result.source = decodeSchema(value.at("source"));
            if (!value.at("target").is_null())
                result.target = decodeSchema(value.at("target"));
            if (result.source.has_value() != result.target.has_value() ||
                ((!result.tables.empty() || !result.rebuildTables.empty()) && !result.source) ||
                (result.mode == MigrationExecutionMode::Normal && !result.rebuildTables.empty()))
            {
                throw MigrationError{ErrorCode::InvalidCatalog,
                                     "Execution contexts require consistent schema declarations"};
            }
            for (const auto* schema : {&result.source, &result.target})
            {
                if (*schema)
                {
                    SchemaSnapshot snapshot;
                    snapshot.backends.emplace((*schema)->backend, **schema);
                    validateSnapshot(snapshot);
                }
            }
            if (result.mode == MigrationExecutionMode::SqliteRebuild &&
                (!result.source || !result.target || result.tables.empty() ||
                 result.source->backend != db::BackendType::Sqlite ||
                 result.target->backend != db::BackendType::Sqlite))
            {
                throw MigrationError{ErrorCode::InvalidCatalog,
                                     "A SQLite rebuild requires its complete schema context"};
            }
            return result;
        });
}

auto decisionsFromJson(std::string_view text) -> GenerationDecisions
{
    return parseMetadata(
        text,
        [](const Json& value)
        {
            if (!value.is_object())
            {
                throw MigrationError{ErrorCode::InvalidCatalog, "Generation decisions must be an object"};
            }
            GenerationDecisions result;
            for (const auto& [key, items] : value.items())
            {
                if (!items.is_array())
                {
                    throw MigrationError{ErrorCode::InvalidCatalog, "Generation decision fields must be arrays"};
                }
                if (key == "tableRenames")
                {
                    for (const auto& item : items)
                    {
                        requireKeys(item, {"from", "to"});
                        result.tableRenames.push_back(
                            {item.at("from").get<std::string>(), item.at("to").get<std::string>()});
                    }
                }
                else if (key == "columnRenames")
                {
                    for (const auto& item : items)
                    {
                        requireKeys(item, {"table", "from", "to"});
                        result.columnRenames.push_back({item.at("table").get<std::string>(),
                                                        item.at("from").get<std::string>(),
                                                        item.at("to").get<std::string>()});
                    }
                }
                else if (key == "allowDataLossUp")
                    result.allowDataLossUp = items.get<std::vector<std::string>>();
                else if (key == "allowDataLossDown")
                    result.allowDataLossDown = items.get<std::vector<std::string>>();
                else
                {
                    auto* target = key == "backfills"       ? &result.backfills :
                                   key == "conversions"     ? &result.conversions :
                                   key == "downBackfills"   ? &result.downBackfills :
                                   key == "downConversions" ? &result.downConversions :
                                                              nullptr;
                    if (target == nullptr)
                    {
                        throw MigrationError{ErrorCode::InvalidCatalog, "Unknown generation decision field"};
                    }
                    for (const auto& item : items)
                        target->push_back(transform(item));
                }
            }
            return result;
        });
}
} // namespace orm::migrations::detail
