module;

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <exception>
#include <format>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <typeinfo>
#include <utility>
#include <vector>

#include "soci/soci.h"

module orm;

import :migrations_internal;
import :database_numeric;

namespace orm::migrations
{
namespace
{
using Access = detail::DatabaseAccess;
auto defaultSessionOperations() -> const detail::SessionOperations&
{
    static const detail::SessionOperations operations;
    return operations;
}

auto sessionOperations(const void* operations) -> const detail::SessionOperations&
{
    // The private typed RunnerAccess factory and the public delegating
    // constructor are the only producers of this borrowed dependency.
    return *static_cast<const detail::SessionOperations*>(operations);
}

[[noreturn]] auto schemaFailure(std::string message) -> void
{
    throw MigrationError{ErrorCode::InvalidSchema, std::move(message)};
}

// SQLite persists CREATE text, including CHECK/default/generated constraints that
// table_info cannot describe. Compare definitions independent of column order and
// whitespace while retaining quoted identifiers, literals and ordered key lists.
auto sqliteDefinitions(std::string_view sql) -> std::vector<std::string>
{
    std::vector<std::string> definitions;
    std::string current;
    int depth = 0;
    char quote = 0;
    bool started = false;
    for (std::size_t i = 0; i < sql.size(); ++i)
    {
        const auto c = sql[i];
        if (quote != 0)
        {
            if (started)
                current += c;
            if (c == quote)
            {
                if (i + 1 < sql.size() && sql[i + 1] == quote)
                {
                    ++i;
                    if (started)
                        current += sql[i];
                }
                else
                {
                    quote = 0;
                }
            }
            continue;
        }
        if (!started && (c == '\'' || c == '"' || c == '`' || c == '['))
        {
            quote = c == '[' ? ']' : c;
            continue;
        }
        if (c == '(')
        {
            if (!started)
            {
                started = true;
                depth = 1;
                continue;
            }
            ++depth;
        }
        if (!started)
        {
            continue;
        }
        if (c == '\'' || c == '"' || c == '`' || c == '[')
        {
            quote = c == '[' ? ']' : c;
        }
        if (c == ')')
        {
            --depth;
            if (depth == 0)
            {
                definitions.push_back(std::move(current));
                current.clear();
                // Keep table options (WITHOUT ROWID, STRICT) in the comparison.
                std::string options;
                for (++i; i < sql.size(); ++i)
                {
                    if (!std::isspace(static_cast<unsigned char>(sql[i])) && sql[i] != ';')
                    {
                        options += static_cast<char>(std::toupper(static_cast<unsigned char>(sql[i])));
                    }
                }
                definitions.push_back(std::move(options));
                break;
            }
        }
        if (c == ',' && depth == 1)
        {
            definitions.push_back(std::move(current));
            current.clear();
        }
        else if (!std::isspace(static_cast<unsigned char>(c)))
        {
            current += quote != 0 ? c : static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
    }
    std::ranges::sort(definitions);
    return definitions;
}

auto nativeType(std::string type) -> std::string
{
    for (auto& c : type)
    {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    const auto open = type.find('(');
    if (open != std::string::npos)
    {
        const auto close = type.find(')', open);
        if (close != std::string::npos)
        {
            type.erase(open, close - open + 1);
        }
    }
    if (type == "CHARACTER VARYING")
        return "VARCHAR";
    if (type == "INTEGER")
        return "INT";
    if (type == "DOUBLE PRECISION")
        return "DOUBLE";
    return type;
}

auto contextPreflight(soci::session& session, db::BackendType backend, const MigrationExecutionContext& context) -> void
{
    if (!context.source)
    {
        return;
    }
    std::string currentSchema;
    if (backend != db::BackendType::Sqlite)
    {
        session << (backend == db::BackendType::Postgres ? "SELECT current_schema()" : "SELECT DATABASE()"),
            soci::into(currentSchema);
        int reserved{};
        session << "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema=" +
                       std::string{backend == db::BackendType::Postgres ? "current_schema()" : "DATABASE()"} +
                       " AND LEFT(table_name,6)='__orm_'",
            soci::into(reserved);
        if (reserved != 0)
            schemaFailure("Reserved migration staging objects already exist in the database");
    }
    for (const auto& name : context.tables)
    {
        const auto source = std::ranges::find(context.source->tables, name, &PhysicalTable::name);
        // Catalog already validates its immutable footprint against the source snapshot.
        if (backend == db::BackendType::Sqlite)
        {
            std::string ddl;
            soci::indicator state = soci::i_null;
            session << "SELECT sql FROM main.sqlite_master WHERE type='table' AND name=:name", soci::use(name, "name"),
                soci::into(ddl, state);
            if (state != soci::i_ok ||
                sqliteDefinitions(ddl) != sqliteDefinitions(detail::renderCreateTable(backend, *source)))
            {
                schemaFailure("Source table differs from its snapshot: " + name);
            }
            soci::rowset<soci::row> indexes =
                (session.prepare << "SELECT name FROM main.sqlite_master WHERE type='index' AND tbl_name=:name "
                                    "AND sql IS NOT NULL",
                 soci::use(name, "name"));
            std::set<std::string> seenIndexes;
            for (const auto& row : indexes)
            {
                const auto indexName = row.get<std::string>(0);
                seenIndexes.insert(indexName);
                const auto modeled = std::ranges::find(source->indexes, indexName, &PhysicalIndex::name);
                if (modeled == source->indexes.end())
                {
                    schemaFailure("Unmanaged index blocks this migration: " + indexName);
                }
                std::vector<std::string> columns;
                int unique{}, partial{};
                session << "SELECT \"unique\",partial FROM pragma_index_list(:table) WHERE name=:name",
                    soci::use(name, "table"), soci::use(indexName, "name"), soci::into(unique), soci::into(partial);
                soci::rowset<soci::row> parts =
                    (session.prepare << "SELECT name,CAST(\"desc\" AS TEXT),coll FROM pragma_index_xinfo(:index) WHERE "
                                        "key=1 ORDER BY seqno",
                     soci::use(indexName, "index"));
                for (const auto& part : parts)
                {
                    if (part.get_indicator(0) == soci::i_null || part.get<std::string>(1) != "0" ||
                        part.get<std::string>(2) != "BINARY")
                        schemaFailure("Expression index differs from its snapshot");
                    columns.push_back(part.get<std::string>(0));
                }
                if (partial != 0 || (unique != 0) != modeled->unique || columns != modeled->columns)
                    schemaFailure("Source index differs from its snapshot: " + indexName);
            }
            for (const auto& index : source->indexes)
                if (!seenIndexes.contains(index.name))
                    schemaFailure("Source index is missing: " + index.name);
        }
        else
        {
            // A destructive footprint must never silently remove unmodelled
            // columns, defaults, triggers, constraints or indexes.
            const bool postgres = backend == db::BackendType::Postgres;
            const auto schema = postgres ? "current_schema()" : "DATABASE()";
            if (!postgres)
            {
                std::string engine, collation, options;
                session << "SELECT engine,table_collation,create_options FROM information_schema.tables "
                           "WHERE table_schema=DATABASE() AND table_name=:name",
                    soci::use(name, "name"), soci::into(engine), soci::into(collation), soci::into(options);
                const auto expectedCollation =
                    backend == db::BackendType::Mysql ? "utf8mb4_0900_bin" : "utf8mb4_nopad_bin";
                if (engine != "InnoDB" || collation != expectedCollation || !options.empty())
                    schemaFailure("Source table options differ from its snapshot: " + name);
            }
            soci::rowset<soci::row> columns =
                (session.prepare
                     << "SELECT column_name,column_default," +
                            std::string{
                                postgres ? "data_type,is_nullable,CAST(character_maximum_length AS BIGINT),is_identity,"
                                           "identity_generation,collation_name,is_generated" :
                                           "column_type,is_nullable,CAST(character_maximum_length AS SIGNED),extra,"
                                           "'' AS identity_generation,collation_name,"
                                           "CASE WHEN generation_expression IS NULL OR generation_expression='' "
                                           "THEN 'NEVER' ELSE 'ALWAYS' END"} +
                            " FROM information_schema.columns WHERE "
                            "table_schema=" +
                            std::string{schema} + " AND table_name=:name",
                 soci::use(name, "name"));
            std::set<std::string> seen;
            for (const auto& row : columns)
            {
                const auto columnName = row.get<std::string>(0);
                const auto expected = std::ranges::find(source->columns, columnName, &PhysicalColumn::name);
                // MariaDB exposes an implicit nullable default as the unquoted
                // text NULL. A literal string default is returned with quotes.
                const auto implicitNullDefault =
                    backend == db::BackendType::Mariadb && expected != source->columns.end() && expected->nullable &&
                    row.get_indicator(1) != soci::i_null && row.get<std::string>(1) == "NULL";
                if (expected == source->columns.end() ||
                    (row.get_indicator(1) != soci::i_null && !expected->autoIncrement && !implicitNullDefault))
                {
                    schemaFailure(
                        std::format("Unmanaged column or default blocks this migration: {}.{}", name, columnName));
                }
                const auto identity = row.get<std::string>(5);
                const auto actualIdentity =
                    postgres ? identity == "YES" : identity.find("auto_increment") != std::string::npos;
                if (nativeType(row.get<std::string>(2)) != nativeType(expected->sqlType) ||
                    (row.get<std::string>(3) == "YES") != expected->nullable ||
                    actualIdentity != expected->autoIncrement || row.get<std::string>(8) != "NEVER" ||
                    (postgres && actualIdentity && row.get<std::string>(6) != "BY DEFAULT") ||
                    (!postgres && identity != (expected->autoIncrement ? "auto_increment" : "")) ||
                    (postgres && row.get_indicator(7) != soci::i_null) ||
                    (!postgres && row.get_indicator(7) != soci::i_null &&
                     row.get<std::string>(7) !=
                         (backend == db::BackendType::Mysql ? "utf8mb4_0900_bin" : "utf8mb4_nopad_bin")) ||
                    (expected->maxStringLength &&
                     (row.get_indicator(4) == soci::i_null ||
                      static_cast<std::size_t>(row.get<long long>(4)) != *expected->maxStringLength)))
                {
                    schemaFailure(std::format("Source column differs from its snapshot: {}.{}", name, columnName));
                }
                seen.insert(columnName);
            }
            if (seen.size() != source->columns.size())
            {
                schemaFailure("Source table columns differ from its snapshot: " + name);
            }
            int triggers{};
            session << "SELECT COUNT(*) FROM information_schema.triggers WHERE event_object_schema=" +
                           std::string{schema} + " AND event_object_table=:name",
                soci::use(name, "name"), soci::into(triggers);
            if (triggers != 0)
            {
                schemaFailure("Unmanaged trigger blocks this migration: " + name);
            }
            const auto constraintQuery =
                postgres ?
                    "SELECT conname,CASE contype WHEN 'p' THEN 'PRIMARY KEY' WHEN 'f' THEN 'FOREIGN KEY' "
                    "WHEN 'n' THEN 'NOT NULL' ELSE 'OTHER' END, "
                    "CASE WHEN condeferrable OR condeferred OR NOT convalidated "
                    "THEN 'unsupported' ELSE 'standard' END FROM pg_constraint "
                    "WHERE conrelid=to_regclass(quote_ident(current_schema())||'.'||quote_ident(:name))" :
                    "SELECT constraint_name,constraint_type,'standard' FROM information_schema.table_constraints "
                    "WHERE table_schema=DATABASE() AND table_name=:name";
            soci::rowset<soci::row> constraints = (session.prepare << constraintQuery, soci::use(name, "name"));
            for (const auto& row : constraints)
            {
                const auto constraint = row.get<std::string>(0);
                const auto type = row.get<std::string>(1);
                if (row.get<std::string>(2) != "standard")
                    schemaFailure(
                        std::format("Source constraint options differ from its snapshot: {}.{}", name, constraint));
                const auto modeledIndex = std::ranges::find(source->indexes, constraint, &PhysicalIndex::name);
                // PostgreSQL represents NOT NULL checks as system constraints.
                if ((type == "PRIMARY KEY" &&
                     (constraint == source->primaryKeyName || (!postgres && constraint == "PRIMARY"))) ||
                    (type == "FOREIGN KEY" &&
                     std::ranges::find(source->foreignKeys, constraint, &PhysicalForeignKey::name) !=
                         source->foreignKeys.end()) ||
                    (postgres && type == "NOT NULL") ||
                    (!postgres && type == "UNIQUE" && modeledIndex != source->indexes.end() && modeledIndex->unique))
                {
                    continue;
                }
                schemaFailure(std::format("Unmanaged constraint blocks this migration: {}.{}", name, constraint));
            }
            if (!source->primaryKey.empty())
            {
                const auto primaryName = postgres ? source->primaryKeyName : std::string{"PRIMARY"};
                soci::rowset<soci::row> parts =
                    (session.prepare << "SELECT column_name FROM information_schema.key_column_usage WHERE "
                                        "table_schema=" +
                                            std::string{schema} +
                                            " AND table_name=:table AND constraint_name=:key ORDER BY ordinal_position",
                     soci::use(name, "table"), soci::use(primaryName, "key"));
                std::vector<std::string> actual;
                for (const auto& part : parts)
                    actual.push_back(part.get<std::string>(0));
                if (actual != source->primaryKey)
                    schemaFailure("Source primary key differs from its snapshot: " + name);
            }
            for (const auto& key : source->foreignKeys)
            {
                soci::rowset<soci::row> parts =
                    (session.prepare
                         << (postgres ?
                                 "SELECT k.column_name,c.table_name,c.column_name,c.table_schema FROM "
                                 "information_schema.key_column_usage k "
                                 "JOIN information_schema.referential_constraints r ON "
                                 "r.constraint_schema=k.constraint_schema "
                                 "AND r.constraint_name=k.constraint_name JOIN information_schema.key_column_usage c "
                                 "ON c.constraint_schema=r.unique_constraint_schema AND "
                                 "c.constraint_name=r.unique_constraint_name "
                                 "AND c.ordinal_position=k.position_in_unique_constraint "
                                 "WHERE k.table_schema=current_schema() AND k.table_name=:table AND "
                                 "k.constraint_name=:key "
                                 "ORDER BY k.ordinal_position" :
                                 "SELECT "
                                 "column_name,referenced_table_name,referenced_column_name,referenced_table_schema "
                                 "FROM "
                                 "information_schema.key_column_usage WHERE table_schema=DATABASE() AND "
                                 "table_name=:table "
                                 "AND constraint_name=:key ORDER BY ordinal_position"),
                     soci::use(name, "table"), soci::use(key.name, "key"));
                std::size_t index = 0;
                for (const auto& part : parts)
                {
                    if (index >= key.columns.size() || part.get<std::string>(0) != key.columns[index] ||
                        part.get<std::string>(1) != key.targetTable ||
                        part.get<std::string>(2) != key.targetColumns[index] ||
                        part.get<std::string>(3) != currentSchema)
                    {
                        schemaFailure("Source foreign key differs from its snapshot: " + key.name);
                    }
                    ++index;
                }
                if (index != key.columns.size())
                    schemaFailure("Source foreign key is missing: " + key.name);
                std::string deleteRule, updateRule;
                session << "SELECT delete_rule,update_rule FROM information_schema.referential_constraints WHERE "
                           "constraint_schema=" +
                               std::string{schema} + " AND constraint_name=:key",
                    soci::use(key.name, "key"), soci::into(deleteRule), soci::into(updateRule);
                const auto rule = [postgres](std::string value)
                { return !postgres && value == "NO ACTION" ? std::string{"RESTRICT"} : value; };
                if (rule(deleteRule) != rule(key.onDelete) || rule(updateRule) != rule(key.onUpdate))
                    schemaFailure("Source foreign key actions differ from its snapshot: " + key.name);
            }
            const auto indexQuery =
                postgres ?
                    "SELECT indexname FROM pg_indexes WHERE schemaname=current_schema() AND tablename=:name" :
                    "SELECT DISTINCT index_name FROM information_schema.statistics WHERE table_schema=DATABASE() "
                    "AND table_name=:name";
            soci::rowset<soci::row> indexes = (session.prepare << indexQuery, soci::use(name, "name"));
            std::set<std::string> seenIndexes;
            for (const auto& row : indexes)
            {
                const auto index = row.get<std::string>(0);
                seenIndexes.insert(index);
                if (index == source->primaryKeyName || (!postgres && index == "PRIMARY") ||
                    std::ranges::find(source->indexes, index, &PhysicalIndex::name) != source->indexes.end())
                {
                    std::vector<std::string> expectedColumns;
                    bool unique{};
                    if (index == source->primaryKeyName || index == "PRIMARY")
                    {
                        expectedColumns = source->primaryKey;
                        unique = true;
                    }
                    else
                    {
                        const auto modeled = std::ranges::find(source->indexes, index, &PhysicalIndex::name);
                        expectedColumns = modeled->columns;
                        unique = modeled->unique;
                    }
                    const auto definitionQuery =
                        postgres ?
                            "SELECT a.attname,CASE WHEN ix.indisunique THEN 'yes' ELSE 'no' END, "
                            "CASE WHEN ix.indpred IS NULL THEN 'full' ELSE 'partial' END, "
                            "CASE WHEN am.amname='btree' AND ix.indisvalid AND ix.indisready AND ix.indislive "
                            "AND ix.indnkeyatts=ix.indnatts AND NOT ix.indnullsnotdistinct "
                            "AND pg_index_column_has_property(i.oid,k.ord::int,'asc') IS TRUE "
                            "AND pg_index_column_has_property(i.oid,k.ord::int,'nulls_last') IS TRUE "
                            "AND opc.opcdefault AND opn.nspname='pg_catalog' "
                            "AND ix.indcollation[(k.ord-1)::int]=a.attcollation "
                            "THEN 'standard' ELSE 'unsupported' END FROM pg_index ix "
                            "JOIN pg_class t ON t.oid=ix.indrelid JOIN pg_namespace n ON n.oid=t.relnamespace "
                            "JOIN pg_class i ON i.oid=ix.indexrelid CROSS JOIN LATERAL unnest(ix.indkey) WITH "
                            "ORDINALITY "
                            "AS k(attnum,ord) LEFT JOIN pg_attribute a ON a.attrelid=t.oid AND a.attnum=k.attnum "
                            "JOIN pg_am am ON am.oid=i.relam "
                            "LEFT JOIN pg_opclass opc ON opc.oid=ix.indclass[(k.ord-1)::int] "
                            "LEFT JOIN pg_namespace opn ON opn.oid=opc.opcnamespace "
                            "WHERE n.nspname=current_schema() AND t.relname=:table AND i.relname=:index ORDER BY "
                            "k.ord" :
                            "SELECT column_name,CASE WHEN non_unique=0 THEN 'yes' ELSE 'no' END, "
                            "CASE WHEN sub_part IS NULL THEN 'full' ELSE 'partial' END, "
                            "CASE WHEN collation='A' AND index_type='BTREE' AND " +
                                std::string{backend == db::BackendType::Mysql ? "is_visible='YES'" : "ignored='NO'"} +
                                " THEN 'standard' ELSE 'unsupported' END FROM "
                                "information_schema.statistics "
                                "WHERE table_schema=DATABASE() AND table_name=:table AND index_name=:index ORDER BY "
                                "seq_in_index";
                    soci::rowset<soci::row> parts =
                        (session.prepare << definitionQuery, soci::use(name, "table"), soci::use(index, "index"));
                    std::vector<std::string> actual;
                    for (const auto& part : parts)
                    {
                        if (part.get_indicator(0) == soci::i_null || (part.get<std::string>(1) == "yes") != unique ||
                            part.get<std::string>(2) != "full" || part.get<std::string>(3) != "standard")
                            schemaFailure(std::format("Source index differs from its snapshot: {}.{}", name, index));
                        actual.push_back(part.get<std::string>(0));
                    }
                    if (actual != expectedColumns)
                        schemaFailure(std::format("Source index columns differ from its snapshot: {}.{}", name, index));
                    continue;
                }
                schemaFailure(std::format("Unmanaged index blocks this migration: {}.{}", name, index));
            }
            for (const auto& index : source->indexes)
                if (!seenIndexes.contains(index.name))
                    schemaFailure("Source index is missing: " + index.name);
            int views{};
            if (postgres)
            {
                session << "SELECT COUNT(*) FROM information_schema.view_table_usage WHERE table_schema=" +
                               std::string{schema} + " AND table_name=:name",
                    soci::use(name, "name"), soci::into(views);
            }
            else
            {
                // MySQL-family view dependency catalog support differs. The
                // initial contract conservatively blocks views in this schema.
                session << "SELECT COUNT(*) FROM information_schema.views WHERE table_schema=DATABASE()",
                    soci::into(views);
            }
            if (views != 0)
            {
                schemaFailure("Unmanaged view blocks this migration: " + name);
            }
        }
    }
    if (backend != db::BackendType::Sqlite && !context.tables.empty())
    {
        const bool postgres = backend == db::BackendType::Postgres;
        for (const auto& name : context.tables)
        {
            const auto inboundQuery =
                postgres ?
                    "SELECT n.nspname,t.relname,c.conname FROM pg_constraint c "
                    "JOIN pg_class t ON t.oid=c.conrelid JOIN pg_namespace n ON n.oid=t.relnamespace "
                    "WHERE c.contype='f' AND "
                    "c.confrelid=to_regclass(quote_ident(current_schema())||'.'||quote_ident(:name))" :
                    "SELECT DISTINCT table_schema,table_name,constraint_name FROM information_schema.key_column_usage "
                    "WHERE referenced_table_schema=DATABASE() AND referenced_table_name=:name";
            soci::rowset<soci::row> inbound = (session.prepare << inboundQuery, soci::use(name, "name"));
            for (const auto& key : inbound)
            {
                const auto ownerName = key.get<std::string>(1);
                const auto constraintName = key.get<std::string>(2);
                const auto owner = std::ranges::find(context.source->tables, ownerName, &PhysicalTable::name);
                if (key.get<std::string>(0) != currentSchema || owner == context.source->tables.end() ||
                    std::ranges::find(context.tables, ownerName) == context.tables.end() ||
                    !std::ranges::any_of(owner->foreignKeys, [&](const auto& expected)
                                         { return expected.name == constraintName && expected.targetTable == name; }))
                    schemaFailure(std::format("Unmanaged incoming foreign key blocks this migration: {}.{}", ownerName,
                                              constraintName));
            }
        }
    }
    if (backend == db::BackendType::Sqlite && context.mode == MigrationExecutionMode::SqliteRebuild)
    {
        int objects{};
        session << "SELECT COUNT(*) FROM main.sqlite_master WHERE type IN ('trigger','view')", soci::into(objects);
        if (objects != 0)
        {
            schemaFailure("SQLite rebuild requires explicit handling of unmanaged triggers and views");
        }
        soci::rowset<soci::row> owners =
            (session.prepare << "SELECT name FROM main.sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'");
        for (const auto& owner : owners)
        {
            const auto ownerName = owner.get<std::string>(0);
            if (std::ranges::find(context.source->tables, ownerName, &PhysicalTable::name) !=
                context.source->tables.end())
                continue;
            soci::rowset<soci::row> keys = (session.prepare << "SELECT \"table\" FROM pragma_foreign_key_list(:name)",
                                            soci::use(ownerName, "name"));
            for (const auto& key : keys)
                if (std::ranges::find(context.tables, key.get<std::string>(0)) != context.tables.end())
                    schemaFailure("Unmanaged inbound foreign key blocks this rebuild: " + ownerName);
        }
        for (const auto& mapping : context.rebuildTables)
        {
            int collision{};
            session << "SELECT COUNT(*) FROM main.sqlite_master WHERE name=:name", soci::use(mapping.staging, "name"),
                soci::into(collision);
            if (collision != 0)
            {
                schemaFailure("SQLite rebuild staging name already exists: " + mapping.staging);
            }
        }
    }
}

auto validateTargetValues(soci::session& session, db::BackendType backend,
                          const MigrationExecutionContext& context) -> void
{
    if (!context.target)
        return;
    for (const auto& table : context.target->tables)
    {
        for (const auto& column : table.columns)
        {
            const auto name = detail::quoteIdentifier(backend, column.name);
            std::string minimum, maximum;
            using enum model::ColumnType;
            switch (column.logicalType)
            {
            case Bool:
                if (backend == db::BackendType::Postgres)
                    continue;
                minimum = "0";
                maximum = "1";
                break;
            case Char:
                minimum = std::to_string(std::numeric_limits<char>::lowest());
                maximum = std::to_string(std::numeric_limits<char>::max());
                break;
            case UnsignedChar:
                minimum = "0";
                maximum = std::to_string(std::numeric_limits<unsigned char>::max());
                break;
            case Short:
                minimum = std::to_string(std::numeric_limits<short>::lowest());
                maximum = std::to_string(std::numeric_limits<short>::max());
                break;
            case UnsignedShort:
                minimum = "0";
                maximum = std::to_string(std::numeric_limits<unsigned short>::max());
                break;
            case Int:
                minimum = std::to_string(std::numeric_limits<int>::lowest());
                maximum = std::to_string(std::numeric_limits<int>::max());
                break;
            case UnsignedInt:
                minimum = "0";
                maximum = std::to_string(std::numeric_limits<unsigned int>::max());
                break;
            case LongLong:
                minimum = std::to_string((std::numeric_limits<long long>::lowest)());
                maximum = std::to_string((std::numeric_limits<long long>::max)());
                break;
            case UnsignedLongLong:
                minimum = "0";
                maximum = backend == db::BackendType::Mysql || backend == db::BackendType::Mariadb ?
                              std::to_string((std::numeric_limits<unsigned long long>::max)()) :
                              std::to_string((std::numeric_limits<long long>::max)());
                break;
            case Float:
            case Double:
                break;
            case String:
            case Uuid:
                continue;
            }
            if (column.logicalType == model::ColumnType::Float || column.logicalType == model::ColumnType::Double)
            {
                if (backend == db::BackendType::Sqlite)
                {
                    long long invalid{};
                    session << std::format(
                        "SELECT COUNT(*) FROM {} WHERE {} IS NOT NULL AND typeof({}) NOT IN ('integer','real')",
                        detail::quoteIdentifier(backend, table.name), name, name),
                        soci::into(invalid);
                    if (invalid != 0)
                        schemaFailure("Migrated floating values have incompatible storage");
                }
                soci::rowset<double> values =
                    (session.prepare << std::format("SELECT {} FROM {} WHERE {} IS NOT NULL", name,
                                                    detail::quoteIdentifier(backend, table.name), name));
                try
                {
                    for (const auto value : values)
                    {
                        if (column.logicalType == model::ColumnType::Float)
                            (void)db::binding::checkedNumericCast<float>(value, column.name);
                        else
                            (void)db::binding::checkedNumericCast<double>(value, column.name);
                    }
                }
                catch (const db::binding::ConversionError&)
                {
                    schemaFailure(std::format("Migrated floating values are outside the target model domain: {}.{}",
                                              table.name, column.name));
                }
                continue;
            }
            auto predicate = std::format("{} < {} OR {} > {}", name, minimum, name, maximum);
            if (backend == db::BackendType::Sqlite)
                predicate += " OR typeof(" + name + ") <> 'integer'";
            long long invalid{};
            session << std::format("SELECT COUNT(*) FROM {} WHERE {} IS NOT NULL AND ({})",
                                   detail::quoteIdentifier(backend, table.name), name, predicate),
                soci::into(invalid);
            if (invalid != 0)
                schemaFailure(
                    std::format("Migrated values are outside the target model domain: {}.{}", table.name, column.name));
        }
    }
}

class SqliteRebuildSession
{
public:
    SqliteRebuildSession(Database& database, const MigrationExecutionContext& context,
                         const detail::SessionOperations& operations)
        : database_{database}, context_{context}, operations_{operations}, previousActive_{Access::active(database)}
    {
        if (context.mode != MigrationExecutionMode::SqliteRebuild)
        {
            return;
        }
        // Catalog validates the backend and both snapshots before a Runner can use this mode.
        Access::setActive(database_, true);
        auto& session = Access::session(database_);
        try
        {
            session << "PRAGMA foreign_keys", soci::into(previousForeignKeys_);
            enabled_ = true;
            session << "PRAGMA foreign_keys=OFF";
            int actual{};
            session << "PRAGMA foreign_keys", soci::into(actual);
            if (actual != 0)
            {
                schemaFailure("Cannot disable SQLite foreign keys before the rebuild transaction");
            }
        }
        catch (...)
        {
            restoreNoThrow();
            Access::setActive(database_, previousActive_);
            throw;
        }
    }
    SqliteRebuildSession(const SqliteRebuildSession&) = delete;
    auto operator=(const SqliteRebuildSession&) -> SqliteRebuildSession& = delete;
    ~SqliteRebuildSession()
    {
        restoreNoThrow();
    }
    auto captureSequences() -> void
    {
        if (!enabled_)
        {
            return;
        }
        auto& session = Access::session(database_);
        int exists{};
        session << "SELECT COUNT(*) FROM main.sqlite_master WHERE name='sqlite_sequence'", soci::into(exists);
        if (exists == 0)
        {
            return;
        }
        for (const auto& table : context_.rebuildTables)
        {
            if (table.sourceIdentity && table.targetIdentity)
            {
                long long sequence{};
                session << "SELECT COALESCE(MAX(seq),0) FROM main.sqlite_sequence WHERE name=:name",
                    soci::use(table.source, "name"), soci::into(sequence);
                sequences_[table.target] = sequence;
            }
        }
    }
    auto validateAndRestoreSequences() -> void
    {
        if (!enabled_)
        {
            return;
        }
        auto& session = Access::session(database_);
        for (const auto& table : context_.rebuildTables)
        {
            if (!table.sourceIdentity || !table.targetIdentity)
            {
                continue;
            }
            long long maximum{}, sequence{};
            int rows{};
            session << "SELECT COALESCE(MAX(" +
                           detail::quoteIdentifier(db::BackendType::Sqlite, *table.targetIdentity) + "),0) FROM " +
                           detail::quoteIdentifier(db::BackendType::Sqlite, table.target),
                soci::into(maximum);
            session << "SELECT COUNT(*),COALESCE(MAX(seq),0) FROM main.sqlite_sequence WHERE name=:name",
                soci::use(table.target, "name"), soci::into(rows), soci::into(sequence);
            const auto preserved = std::max({maximum, sequence, sequences_[table.target]});
            if (rows == 0)
            {
                session << "INSERT INTO main.sqlite_sequence(name,seq) VALUES(:name,:seq)",
                    soci::use(table.target, "name"), soci::use(preserved, "seq");
            }
            else
            {
                session << "UPDATE main.sqlite_sequence SET seq=:seq WHERE name=:name", soci::use(preserved, "seq"),
                    soci::use(table.target, "name");
            }
        }
        soci::rowset<soci::row> violations = (session.prepare << "PRAGMA main.foreign_key_check");
        if (violations.begin() != violations.end())
        {
            schemaFailure("SQLite rebuild failed foreign_key_check");
        }
    }
    auto restoreAfterCommit(Version version) -> void
    {
        try
        {
            restore();
        }
        catch (...)
        {
            database_.disconnect();
            throw MigrationError{ErrorCode::SessionRestoration,
                                 "Migration committed, but session restoration failed; reconnect and inspect history",
                                 version, db::BackendType::Sqlite};
        }
    }

private:
    Database& database_;
    const MigrationExecutionContext& context_;
    const detail::SessionOperations& operations_;
    bool previousActive_{};
    int previousForeignKeys_{};
    bool enabled_{};
    std::map<std::string, long long> sequences_;
    auto restore() -> void
    {
        if (!enabled_)
        {
            if (context_.mode == MigrationExecutionMode::SqliteRebuild)
            {
                Access::setActive(database_, previousActive_);
            }
            return;
        }
        enabled_ = false;
        if (database_.isConnected())
        {
            operations_.restoreSqliteForeignKeys(Access::session(database_), previousForeignKeys_);
        }
        Access::setActive(database_, previousActive_);
    }
    auto restoreNoThrow() noexcept -> void
    {
        try
        {
            restore();
        }
        catch (...)
        {
            // Ownership excludes an ORM transaction or pooled session, so closing
            // cannot produce an ORM disconnect rollback error.
            database_.disconnect();
            Access::setActive(database_, previousActive_);
        }
    }
};

} // namespace

Runner::Runner(Database& database, Catalog catalog)
    : Runner{database, std::move(catalog), std::addressof(defaultSessionOperations())}
{
}

Runner::Runner(Database& database, Catalog catalog, const void* operations)
    : database_{&database}, catalog_{std::move(catalog)}, operations_{operations}
{
}

auto Runner::status() const -> Status
{
    return detail::MigrationState{*database_, catalog_, sessionOperations(operations_)}.status();
}

auto Runner::validate() const -> void
{
    detail::MigrationState{*database_, catalog_, sessionOperations(operations_)}.validate();
}

auto Runner::preview(Direction direction, std::optional<Version> target) const -> Plan
{
    detail::MigrationState state{*database_, catalog_, sessionOperations(operations_)};
    return state.plan(state.status(), direction, target);
}

auto Runner::up(std::optional<Version> target) -> void
{
    execute(Direction::Up, target);
}
auto Runner::down(Version target) -> void
{
    execute(Direction::Down, target);
}

auto Runner::execute(Direction direction, std::optional<Version> target) -> void
{
    const auto& operations = sessionOperations(operations_);
    detail::MigrationState state{*database_, catalog_, operations};
    const auto initial = state.status();
    const auto plan = state.plan(initial, direction, target);
    auto expected = initial.applied;
    const auto backend = database_->getBackendType();
    auto& session = Access::session(*database_);
    const auto& runtime = Access::runtime(*database_);
    for (const auto& step : plan.migrations)
    {
        try
        {
            const auto& migration = *std::ranges::find(catalog_.migrations(), step.version, &Migration::version);
            // MigrationState validated every catalog entry before planning.
            const auto& scripts = migration.scripts.at(backend);
            const auto& context = direction == Direction::Up ? scripts.upContext : scripts.downContext;
            SqliteRebuildSession rebuild{*database_, context, operations};
            expected = state.apply(
                migration, direction, expected,
                [&]
                {
                    contextPreflight(session, backend, context);
                    rebuild.captureSequences();
                },
                [&]
                {
                    rebuild.validateAndRestoreSequences();
                    validateTargetValues(session, backend, context);
                });
            rebuild.restoreAfterCommit(step.version);
        }
        catch (const soci::soci_error& error)
        {
            const auto translated = runtime.translateError(error, DatabaseErrorCode::Statement, "migrations");
            throw MigrationError{ErrorCode::Execution, "Migration SQL or history operation failed", step.version,
                                 backend, translated.getNativeCode()};
        }
        catch (const MigrationError& error)
        {
            throw MigrationError{error.getCode(), error.what(), step.version, backend, error.getNativeCode()};
        }
    }
}

auto Runner::baseline(Version target) -> void
{
    detail::MigrationState{*database_, catalog_, sessionOperations(operations_)}.baseline(target);
}

auto Runner::resolve(Version version, std::string_view expectedChecksum, Resolution resolution) -> void
{
    detail::MigrationState{*database_, catalog_, sessionOperations(operations_)}.resolve(version, expectedChecksum,
                                                                                         resolution);
}
} // namespace orm::migrations
