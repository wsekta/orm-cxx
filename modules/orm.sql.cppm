module;

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <charconv>
#include <cmath>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>
#include "soci/soci.h"
#include "soci/values.h"

export module orm:sql;

import orm.reflection;
import :foundation;
import :model;
import :expressions;
import :dynamic_query;
import :static_plan;

// database/BackendCapabilities.hpp
namespace orm::db
{
export {

enum class AffectedRowsSupport
{
    Unavailable,
    Reliable,
};

struct SchemaCapabilities
{
    bool createTableIfNotExists = false;
    bool dropTableIfExists = false;
    bool autoIncrementPrimaryKey = false;
    bool compositePrimaryKeys = false;
    bool foreignKeys = false;
    bool onDeleteCascade = false;

    auto operator==(const SchemaCapabilities&) const -> bool = default;
};

struct QueryCapabilities
{
    bool limit = false;
    bool offset = false;
    bool offsetWithoutLimit = false;
    bool offsetRequiresOrderBy = false;
    bool projections = false;
    bool groupBy = false;
    bool having = false;
    bool collectionPredicates = false;
    bool fullModelGrouping = false;
    bool distinctOrderByRequiresProjectedColumn = false;
    bool strictProjectionGrouping = false;

    auto operator==(const QueryCapabilities&) const -> bool = default;
};

struct MutationCapabilities
{
    bool insert = false;
    bool update = false;
    bool remove = false;
    bool atomicInsertIfAbsent = false;
    AffectedRowsSupport affectedRows = AffectedRowsSupport::Unavailable;

    auto operator==(const MutationCapabilities&) const -> bool = default;
};

struct RelationCapabilities
{
    bool toOne = false;
    bool oneToMany = false;
    bool manyToMany = false;
    bool collectionIncludes = false;
    bool collectionPredicates = false;
    bool junctionTables = false;
    bool compositeEndpointKeys = false;

    auto operator==(const RelationCapabilities&) const -> bool = default;
};

struct BackendValueLimits
{
    /**
     * Largest unsigned 64-bit value that can be exchanged without loss.
     * An empty value means that the full C++ range is supported.
     */
    std::optional<unsigned long long> maxUnsignedLongLong;

    auto operator==(const BackendValueLimits&) const -> bool = default;
};

struct BackendCapabilities
{
    SchemaCapabilities schema;
    QueryCapabilities query;
    MutationCapabilities mutations;
    RelationCapabilities relations;
    BackendValueLimits valueLimits;
    std::vector<model::ColumnType> supportedColumnTypes;
    bool transactions = false;

    [[nodiscard]] auto supportsColumnType(model::ColumnType type) const -> bool
    {
        return std::ranges::find(supportedColumnTypes, type) != supportedColumnTypes.end();
    }

    auto operator==(const BackendCapabilities&) const -> bool = default;
};

struct BackendRuntimeLimits
{
    std::optional<std::size_t> maxBindParameters;

    auto operator==(const BackendRuntimeLimits&) const -> bool = default;
};

}
} // namespace orm::db

// database/BackendProvider.hpp
namespace orm::db
{
export {

class BackendRuntime;
class CommandGenerator;
class SqlDialect;

class BackendProvider
{
public:
    virtual ~BackendProvider() = default;

    [[nodiscard]] virtual auto type() const noexcept -> BackendType = 0;
    [[nodiscard]] virtual auto acceptsConnectionString(std::string_view connectionString) const noexcept -> bool = 0;
    [[nodiscard]] virtual auto capabilities() const noexcept -> const BackendCapabilities& = 0;
    [[nodiscard]] virtual auto dialect() const noexcept -> const SqlDialect& = 0;
    [[nodiscard]] virtual auto runtime() const noexcept -> const BackendRuntime& = 0;
    [[nodiscard]] virtual auto commandGenerator() const noexcept -> const CommandGenerator& = 0;
    [[nodiscard]] virtual auto compiledSqlFlavor() const noexcept -> CompiledSqlFlavor
    {
        return CompiledSqlFlavor::None;
    }
};

}
} // namespace orm::db

// database/Statement.hpp
namespace orm::db
{
export {

struct BoundValue
{
    model::ColumnType logicalType;
    std::optional<query::QueryValue::Value> value;

    [[nodiscard]] auto isNull() const noexcept -> bool
    {
        return not value.has_value();
    }
};

struct StatementParameter
{
    std::string name;
    std::optional<query::QueryValue> value;
    std::optional<model::ColumnType> nullType;

    [[nodiscard]] auto getLogicalType() const -> model::ColumnType
    {
        if (value.has_value())
        {
            return value->getLogicalType();
        }
        if (nullType.has_value())
        {
            return nullType.value();
        }
        throw std::invalid_argument{"A NULL statement parameter requires an explicit logical type"};
    }

    [[nodiscard]] auto getBoundValue() const -> BoundValue
    {
        if (value.has_value())
        {
            return BoundValue{.logicalType = value->getLogicalType(), .value = value->get()};
        }

        return BoundValue{.logicalType = getLogicalType(), .value = std::nullopt};
    }
};

struct Statement
{
    std::string sql;
    std::vector<StatementParameter> parameters;
};

/** Non-owning statement input; compiled SQL has static storage duration. */
struct StatementView
{
    std::string_view sql;
    std::span<const StatementParameter> parameters;
};

}
} // namespace orm::db

// database/BackendRuntime.hpp
namespace orm::db
{
export {

class BackendRuntime
{
public:
    virtual ~BackendRuntime() = default;

    virtual auto open(soci::session& session, std::string_view connectionString) const -> void = 0;
    virtual auto onConnect(soci::session& session) const -> void = 0;
    [[nodiscard]] virtual auto tableExists(soci::session& session, std::string_view tableName) const -> bool = 0;
    [[nodiscard]] virtual auto limits(soci::session& session) const -> BackendRuntimeLimits = 0;
    [[nodiscard]] virtual auto normalizeAffectedRows(long long affectedRows) const -> std::size_t = 0;
    [[nodiscard]] virtual auto
    statementErrorInvalidatesTransaction(const soci::soci_error& /*error*/) const noexcept -> bool
    {
        return false;
    }
    virtual auto bind(soci::values& values, std::string_view name, const BoundValue& value) const -> void = 0;
    [[nodiscard]] virtual auto translateError(const soci::soci_error& error, DatabaseErrorCode fallback,
                                              std::string_view operation) const -> DatabaseError = 0;
};

}
} // namespace orm::db

// database/commands/CreateTableCommand.hpp
namespace orm::db::commands
{
export {

class CreateTableCommand
{
public:
    virtual ~CreateTableCommand() = default;

    [[nodiscard]] virtual auto createTable(model::ModelView model) const -> std::string = 0;
};

}
} // namespace orm::db

// database/commands/DeleteCommand.hpp
namespace orm::db::commands
{
export {

class DeleteCommand
{
public:
    virtual ~DeleteCommand() = default;

    [[nodiscard]] virtual auto remove(model::ModelView model,
                                      const query::detail::Predicate& predicate) const -> Statement = 0;
};

}
} // namespace orm::db::commands

// database/commands/DropTableCommand.hpp
namespace orm::db::commands
{
export {

class DropTableCommand
{
public:
    virtual ~DropTableCommand() = default;

    [[nodiscard]] virtual auto dropTable(model::ModelView model) const -> std::string = 0;
};

}
}

// database/commands/InsertCommand.hpp
namespace orm::db::commands
{
export {

class InsertCommand
{
public:
    virtual ~InsertCommand() = default;

    [[nodiscard]] virtual auto insert(model::ModelView model) const -> std::string = 0;
};

}
}

// database/SelectStatement.hpp
namespace orm::db
{
export {

using SelectStatement = Statement;

}
} // namespace orm::db

// database/commands/SelectCommand.hpp
namespace orm::db::commands
{
export {

class SelectCommand
{
public:
    virtual ~SelectCommand() = default;

    [[nodiscard]] virtual auto select(model::ModelView model,
                                      const query::detail::SelectSpec& spec) const -> SelectStatement = 0;
};

}
} // namespace orm::db::commands

// database/commands/UpdateCommand.hpp
namespace orm::db::commands
{
export {

class UpdateCommand
{
public:
    virtual ~UpdateCommand() = default;

    [[nodiscard]] virtual auto update(model::ModelView model,
                                      const query::detail::UpdateSpec& spec) const -> Statement = 0;
};

}
} // namespace orm::db::commands

// database/CommandGenerator.hpp
namespace orm::db
{
export {

class CommandGenerator
{
public:
    CommandGenerator(std::unique_ptr<commands::CreateTableCommand> createTableCommand,
                     std::unique_ptr<commands::DropTableCommand> dropTableCommand,
                     std::unique_ptr<commands::InsertCommand> insertCommand,
                     std::unique_ptr<commands::SelectCommand> selectCommand,
                     std::unique_ptr<commands::UpdateCommand> updateCommand,
                     std::unique_ptr<commands::DeleteCommand> deleteCommand);

    [[nodiscard]] auto createTable(model::ModelView model) const -> std::string;
    [[nodiscard]] auto dropTable(model::ModelView model) const -> std::string;
    [[nodiscard]] auto insert(model::ModelView model) const -> std::string;
    [[nodiscard]] auto select(model::ModelView model, const query::detail::SelectSpec& spec) const -> SelectStatement;
    [[nodiscard]] auto update(model::ModelView model, const query::detail::UpdateSpec& spec) const -> Statement;
    [[nodiscard]] auto remove(model::ModelView model, const query::detail::Predicate& predicate) const -> Statement;

private:
    std::unique_ptr<commands::CreateTableCommand> createTableCommand;
    std::unique_ptr<commands::DropTableCommand> dropTableCommand;
    std::unique_ptr<commands::InsertCommand> insertCommand;
    std::unique_ptr<commands::SelectCommand> selectCommand;
    std::unique_ptr<commands::UpdateCommand> updateCommand;
    std::unique_ptr<commands::DeleteCommand> deleteCommand;
};

}
} // namespace orm::db

// database/CommandGeneratorFactory.hpp
namespace orm::db
{
export {

class CommandGeneratorFactory
{
public:
    CommandGeneratorFactory();
    CommandGeneratorFactory(const CommandGeneratorFactory&) = delete;
    CommandGeneratorFactory(CommandGeneratorFactory&&) = default;
    auto operator=(const CommandGeneratorFactory&) -> CommandGeneratorFactory& = delete;
    auto operator=(CommandGeneratorFactory&&) -> CommandGeneratorFactory& = default;

    auto registerBackend(std::unique_ptr<BackendProvider> backend) -> void;
    [[nodiscard]] auto getBackend(BackendType backendType) const -> const BackendProvider&;
    [[nodiscard]] auto findBackend(std::string_view connectionString) const noexcept -> const BackendProvider*;
    auto getCommandGenerator(BackendType backendType) const -> const CommandGenerator&;

private:
    std::unordered_map<BackendType, std::unique_ptr<BackendProvider>> backends;
};

}
} // namespace orm::db

// Primary-key storage shared by SQL statements and model binding.
namespace orm::db::binding { using PrimaryKey = std::vector<query::QueryValue>; }

// database/SqlDialect.hpp
namespace orm::db
{
export {

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
    [[nodiscard]] virtual auto renderCreateTablePrefix(std::string_view tableName,
                                                       bool ifNotExists) const -> std::string = 0;
    [[nodiscard]] virtual auto renderDropTable(std::string_view tableName, bool ifExists) const -> std::string = 0;
    [[nodiscard]] virtual auto renderAutoIncrementPrimaryKey(std::string_view columnName) const -> std::string = 0;
    [[nodiscard]] virtual auto renderPagination(const PaginationSpec& pagination) const -> std::string = 0;
    [[nodiscard]] virtual auto renderInsertIfAbsent(const InsertIfAbsentSpec& insert) const -> std::string = 0;
    [[nodiscard]] virtual auto renderAggregateResult(std::string_view expression,
                                                     bool /*preserveExactNumeric*/) const -> std::string
    {
        return std::string{expression};
    }
};

}
} // namespace orm::db

// database/RelationStatements.hpp
namespace orm::db::relations
{
export {

[[nodiscard]] auto createTableStatements(const SqlDialect& dialect, model::ModelView owner) -> std::vector<std::string>;
[[nodiscard]] auto dropTableStatements(const SqlDialect& dialect, model::ModelView owner) -> std::vector<std::string>;

[[nodiscard]] auto linkStatement(const SqlDialect& dialect, model::ModelView owner, model::RelationView relation,
                                 const binding::PrimaryKey& ownerKey,
                                 const binding::PrimaryKey& targetKey) -> Statement;
[[nodiscard]] auto unlinkStatement(const SqlDialect& dialect, model::ModelView owner, model::RelationView relation,
                                   const binding::PrimaryKey& ownerKey,
                                   const binding::PrimaryKey& targetKey) -> Statement;

[[nodiscard]] auto collectionSelectStatement(const SqlDialect& dialect, model::ModelView owner,
                                             model::RelationView relation, std::string targetSelectSql,
                                             const std::vector<binding::PrimaryKey>& ownerKeys,
                                             bool joinedValues) -> Statement;

}
} // namespace orm::db::relations

// database/SqlNameValidation.hpp
namespace orm::db::detail
{
[[nodiscard]] constexpr auto isPortableBindName(std::string_view name) noexcept -> bool
{
    if (name.empty())
    {
        return false;
    }

    for (const auto character : name)
    {
        const auto asciiLetter = (character >= 'a' and character <= 'z') or (character >= 'A' and character <= 'Z');
        const auto asciiDigit = character >= '0' and character <= '9';

        if (not asciiLetter and not asciiDigit and character != '_')
        {
            return false;
        }
    }

    return true;
}
} // namespace orm::db::detail

// database/SqlEmitter.hpp
namespace orm::db
{
namespace detail
{
inline constexpr auto noSqlNode = std::numeric_limits<std::size_t>::max();
enum class SqlOperation
{
    Select,
    Update,
    Remove
};
enum class SqlNodeKind
{
    Comparison,
    Null,
    List,
    Between,
    And,
    Or,
    Not,
    Collection,
    Raw,
    Invalid
};

struct SqlSource
{
    std::array<std::string_view, 2> pathParts{};
    std::size_t pathSize{};
    bool isAggregate{};
    query::detail::AggregateFunction function{};
};

struct SqlNode
{
    SqlNodeKind kind{};
    SqlSource source{};
    unsigned operation{};
    std::size_t left = noSqlNode;
    std::size_t right = noSqlNode;
    std::size_t firstParameter{};
    std::size_t parameterCount{};
    std::string_view relation{};
    std::string_view rawSql{};
};

struct SqlOrder
{
    SqlSource source{};
    query::detail::OrderDirection direction{};
    bool isRaw{};
    std::string_view rawSql{};
};
struct SqlProjection
{
    SqlSource source{};
    std::string_view alias{};
};
struct SqlAssignment
{
    SqlSource source{};
    std::size_t parameter{};
};
struct SqlBindingDescriptor
{
    model::ColumnType logicalType{};
    std::size_t index{};
    std::string_view rawName{};
};

struct SqlQueryView
{
    SqlOperation operation{};
    std::span<const SqlNode> nodes{};
    std::size_t predicate = noSqlNode;
    std::size_t having = noSqlNode;
    std::span<const SqlOrder> orders{};
    std::span<const SqlSource> groups{};
    std::span<const SqlProjection> projections{};
    std::span<const SqlAssignment> assignments{};
    std::span<const std::string_view> includes{};
    std::span<const SqlBindingDescriptor> bindings{};
    bool isDistinct{};
    bool shouldJoin = true;
    bool hasLimit{};
    bool hasOffset{};
    bool boundPagination{};
    std::size_t limitParameter{};
    std::size_t offsetParameter{};
    std::size_t literalLimit{};
    std::size_t literalOffset{};
};

// Temporary storage is also valid during C++20 constant evaluation. The
// compiled statement copies it into exactly sized arrays before returning.
struct SqlProgram
{
    SqlOperation operation{};
    std::vector<SqlNode> nodes;
    std::size_t predicate = noSqlNode;
    std::size_t having = noSqlNode;
    std::vector<SqlOrder> orders;
    std::vector<SqlSource> groups;
    std::vector<SqlProjection> projections;
    std::vector<SqlAssignment> assignments;
    std::vector<std::string_view> includes;
    std::vector<SqlBindingDescriptor> bindings;
    bool isDistinct{};
    bool shouldJoin = true;
    bool hasLimit{};
    bool hasOffset{};
    bool boundPagination{};
    std::size_t limitParameter{};
    std::size_t offsetParameter{};
    std::size_t literalLimit{};
    std::size_t literalOffset{};

    [[nodiscard]] constexpr auto view() const -> SqlQueryView
    {
        return {operation,       nodes,          predicate,       having,       orders,       groups,   projections,
                assignments,     includes,       bindings,        isDistinct,   shouldJoin,   hasLimit, hasOffset,
                boundPagination, limitParameter, offsetParameter, literalLimit, literalOffset};
    }
};

[[nodiscard]] constexpr auto decimal(std::size_t value) -> std::string
{
    std::string result;
    do
    {
        result.push_back(static_cast<char>('0' + value % 10));
        value /= 10;
    } while (value != 0);
    std::ranges::reverse(result);
    return result;
}
template <typename... Parts>
[[nodiscard]] constexpr auto sqlConcat(const Parts&... parts) -> std::string
{
    std::string result;
    (result.append(parts), ...);
    return result;
}
[[nodiscard]] constexpr auto parameterName(const SqlBindingDescriptor& descriptor) -> std::string
{
    return descriptor.rawName.empty() ? sqlConcat("orm_p", decimal(descriptor.index)) : std::string{descriptor.rawName};
}
[[nodiscard]] constexpr auto quoteStandardIdentifier(std::string_view identifier,
                                                     CompiledSqlFlavor flavor) -> std::string
{
    if (identifier.empty())
        throw std::invalid_argument{"SQL identifier cannot be empty"};
    if (identifier.find('\0') != std::string_view::npos)
        throw std::invalid_argument{flavor == CompiledSqlFlavor::PostgreSQL ?
                                        "PostgreSQL identifiers must not contain an embedded NUL byte" :
                                        "SQLite identifiers must not contain an embedded NUL byte"};
    if (flavor == CompiledSqlFlavor::PostgreSQL && identifier.size() > 63)
        throw std::invalid_argument{"PostgreSQL identifiers must not exceed 63 bytes"};
    std::string result{"\""};
    for (const auto character : identifier)
    {
        if (character == '"')
            result += '"';
        result += character;
    }
    result += '"';
    return result;
}

template <CompiledSqlFlavor Flavor>
struct StaticSqlPolicy
{
    [[nodiscard]] constexpr auto quoteIdentifier(std::string_view name) const -> std::string
    {
        return quoteStandardIdentifier(name, Flavor);
    }
    [[nodiscard]] constexpr auto bindMarker(std::string_view name) const -> std::string
    {
        if (!isPortableBindName(name))
            throw std::invalid_argument{"Invalid SQL bind parameter name"};
        return sqlConcat(":", name);
    }
    [[nodiscard]] constexpr auto renderAggregateResult(std::string_view value, bool exact) const -> std::string
    {
        if constexpr (Flavor == CompiledSqlFlavor::PostgreSQL)
            if (exact)
                return sqlConcat("CAST(", value, " AS TEXT)");
        return std::string{value};
    }
    [[nodiscard]] constexpr auto pagination(const SqlQueryView& query) const -> std::string
    {
        std::string result;
        if (query.hasLimit)
        {
            result += " LIMIT ";
            result += query.boundPagination ? bindMarker(parameterName(query.bindings[query.limitParameter])) :
                                              decimal(query.literalLimit);
        }
        else if constexpr (Flavor == CompiledSqlFlavor::SQLite)
            if (query.hasOffset)
                result += " LIMIT -1";
        if (query.hasOffset)
        {
            result += " OFFSET ";
            result += query.boundPagination ? bindMarker(parameterName(query.bindings[query.offsetParameter])) :
                                              decimal(query.literalOffset);
        }
        return result;
    }
};

struct RuntimeSqlPolicy
{
    const SqlDialect& dialect;
    [[nodiscard]] auto quoteIdentifier(std::string_view name) const -> std::string
    {
        return dialect.quoteIdentifier(name);
    }
    [[nodiscard]] auto bindMarker(std::string_view name) const -> std::string
    {
        return dialect.bindMarker(name);
    }
    [[nodiscard]] auto renderAggregateResult(std::string_view value, bool exact) const -> std::string
    {
        return dialect.renderAggregateResult(value, exact);
    }
    [[nodiscard]] auto pagination(const SqlQueryView& query) const -> std::string
    {
        if (query.boundPagination)
            throw std::logic_error{"A runtime dialect requires literal pagination"};
        return dialect.renderPagination(
            PaginationSpec{.limit = query.hasLimit ? std::optional{query.literalLimit} : std::nullopt,
                           .offset = query.hasOffset ? std::optional{query.literalOffset} : std::nullopt,
                           .hasOrderBy = !query.orders.empty()});
    }
};

[[nodiscard]] constexpr auto sqlPath(const SqlSource& source) -> std::string
{
    if (source.pathSize == 0)
        return {};
    return source.pathSize == 1 ? std::string{source.pathParts[0]} :
                                  sqlConcat(source.pathParts[0], ".", source.pathParts[1]);
}
[[nodiscard]] constexpr auto parseSqlSource(std::string_view path) -> SqlSource
{
    SqlSource result;
    std::size_t start = 0;
    while (start <= path.size())
    {
        const auto dot = path.find('.', start);
        const auto part = path.substr(start, dot - start);
        if (part.empty())
            throw std::invalid_argument{sqlConcat("Column path contains an empty segment: ", path)};
        if (result.pathSize == result.pathParts.size())
            throw std::invalid_argument{sqlConcat("Only one level of related model paths is supported: ", path)};
        result.pathParts[result.pathSize++] = part;
        if (dot == std::string_view::npos)
            break;
        start = dot + 1;
    }
    return result;
}
[[nodiscard]] constexpr auto requireSqlColumn(model::ModelView model, std::string_view name) -> const model::ColumnView&
{
    const auto* column = model.findColumn(name);
    if (column == nullptr)
        throw std::invalid_argument{sqlConcat("Unknown column path segment: ", name)};
    return *column;
}
[[nodiscard]] constexpr auto requireSqlTarget(model::ModelView model,
                                              const model::ColumnView& column) -> model::ModelView
{
    if (column.kind != model::FieldKind::ToOne)
        throw std::invalid_argument{sqlConcat("Column is not a related model: ", column.fieldName)};
    const auto target = model.resolveTarget(column);
    if (!target)
        throw std::logic_error{"To-one relation target is not available in the schema"};
    return target;
}
[[nodiscard]] constexpr auto sqlPrimaryKey(model::ModelView model) -> std::vector<const model::ColumnView*>
{
    std::vector<const model::ColumnView*> result;
    for (const auto& column : model->columns)
        if (column.isPrimaryKey)
        {
            if (column.kind == model::FieldKind::ToOne)
                throw std::invalid_argument{"Relations with model-valued primary-key fields are not supported"};
            result.push_back(&column);
        }
    if (result.empty())
        throw std::invalid_argument{"Collection relation endpoint must define a primary key"};
    return result;
}
struct SqlColumnInfo
{
    const model::ColumnView* column{};
    const model::ColumnView* relation{};
    model::ColumnType logicalType{};
    bool isNotNull{};
};
[[nodiscard]] constexpr auto resolveSqlColumn(const SqlSource& source, model::ModelView model,
                                              bool write = false) -> SqlColumnInfo
{
    if (source.pathSize == 1)
    {
        const auto& column = requireSqlColumn(model, source.pathParts[0]);
        if (column.kind == model::FieldKind::ToOne)
            throw std::invalid_argument{sqlConcat(write ?
                                                      "Use a related primary-key field path in write queries: " :
                                                      "Use a related field path instead of the related model itself: ",
                                                  sqlPath(source))};
        return {&column, nullptr, column.type.value(), column.isNotNull};
    }
    if (source.pathSize == 2)
    {
        const auto& relation = requireSqlColumn(model, source.pathParts[0]);
        const auto target = requireSqlTarget(model, relation);
        const auto& column = requireSqlColumn(target, source.pathParts[1]);
        if (write && !column.isPrimaryKey)
            throw std::invalid_argument{
                sqlConcat("Only related primary-key fields can be used in write queries: ", sqlPath(source))};
        return {&column, &relation, column.type.value(), relation.isNotNull};
    }
    throw std::invalid_argument{sqlConcat("Only one level of related model paths is supported: ", sqlPath(source))};
}

struct SqlRenderScope
{
    model::ModelView model;
    bool shouldJoin = true;
    bool write{};
    bool allowCollections = true;
    std::string tableAlias;
    std::vector<std::pair<std::string, std::string>> relationAliases;
};
template <typename Policy>
[[nodiscard]] constexpr auto sqlQualified(const Policy& policy, std::string_view qualifier,
                                          std::string_view name) -> std::string
{
    return sqlConcat(policy.quoteIdentifier(qualifier), ".", policy.quoteIdentifier(name));
}
template <typename Policy>
[[nodiscard]] constexpr auto emitSqlColumn(const SqlSource& source, const SqlRenderScope& scope, const Policy& policy,
                                           bool qualify = true) -> std::string
{
    const auto info = resolveSqlColumn(source, scope.model, scope.write);
    const auto root = scope.tableAlias.empty() ? scope.model->tableName : std::string_view{scope.tableAlias};
    if (info.relation == nullptr)
        return qualify ? sqlQualified(policy, root, info.column->name) : policy.quoteIdentifier(info.column->name);
    if (scope.write || !scope.shouldJoin)
    {
        if (!info.column->isPrimaryKey)
            throw std::invalid_argument{
                sqlConcat("Cannot filter by non-id related field when joining is disabled: ", sqlPath(source))};
        const auto name = sqlConcat(info.relation->name, "_", info.column->name);
        return qualify ? sqlQualified(policy, root, name) : policy.quoteIdentifier(name);
    }
    auto qualifier = info.relation->name;
    for (const auto& entry : scope.relationAliases)
        if (entry.first == qualifier)
        {
            qualifier = entry.second;
            break;
        }
    return sqlQualified(policy, qualifier, info.column->name);
}
[[nodiscard]] constexpr auto sqlComparison(unsigned operation) -> std::string_view
{
    using enum query::detail::ComparisonOperator;
    switch (static_cast<query::detail::ComparisonOperator>(operation))
    {
    case Equal:
        return "=";
    case NotEqual:
        return "!=";
    case Greater:
        return ">";
    case GreaterOrEqual:
        return ">=";
    case Less:
        return "<";
    case LessOrEqual:
        return "<=";
    case Like:
        return "LIKE";
    case NotLike:
        return "NOT LIKE";
    }
    throw std::invalid_argument{"Unsupported comparison operator"};
}
[[nodiscard]] constexpr auto sqlAggregateName(query::detail::AggregateFunction function) -> std::string_view
{
    using enum query::detail::AggregateFunction;
    switch (function)
    {
    case Count:
    case CountAll:
        return "COUNT";
    case Sum:
        return "SUM";
    case Avg:
        return "AVG";
    case Min:
        return "MIN";
    case Max:
        return "MAX";
    }
    throw std::invalid_argument{"Unsupported aggregate function"};
}
template <typename Policy>
[[nodiscard]] constexpr auto emitSqlSource(const SqlSource& source, const SqlRenderScope& scope, const Policy& policy,
                                           bool projection = false) -> std::string
{
    if (!source.isAggregate)
        return emitSqlColumn(source, scope, policy);
    auto result = sqlConcat(sqlAggregateName(source.function), "(");
    result +=
        source.function == query::detail::AggregateFunction::CountAll ? "*" : emitSqlColumn(source, scope, policy);
    result += ')';
    return projection ?
               policy.renderAggregateResult(result, source.function == query::detail::AggregateFunction::Sum ||
                                                        source.function == query::detail::AggregateFunction::Avg) :
               result;
}
[[nodiscard]] constexpr auto hasSqlAlias(const std::vector<std::string>& names, std::string_view name) -> bool
{
    return std::ranges::find(names, name) != names.end();
}
[[nodiscard]] constexpr auto uniqueSqlAlias(std::string_view base, const std::vector<std::string>& names) -> std::string
{
    std::string result{base};
    std::size_t suffix = 1;
    while (hasSqlAlias(names, result))
        result = sqlConcat(base, "_", decimal(suffix++));
    return result;
}
template <typename Policy>
[[nodiscard]] constexpr auto emitSqlJoins(const SqlRenderScope& scope, const Policy& policy) -> std::string
{
    std::string result;
    for (const auto& relation : scope.model->columns)
    {
        if (relation.kind != model::FieldKind::ToOne)
            continue;
        const auto target = requireSqlTarget(scope.model, relation);
        auto alias = std::string{relation.name};
        for (const auto& entry : scope.relationAliases)
            if (entry.first == relation.name)
            {
                alias = entry.second;
                break;
            }
        result += sqlConcat(" LEFT JOIN ", policy.quoteIdentifier(target->tableName), " AS ",
                            policy.quoteIdentifier(alias), " ON ");
        bool first = true;
        for (const auto& column : target->columns)
            if (column.isPrimaryKey)
            {
                if (!first)
                    result += " AND ";
                first = false;
                const auto root =
                    scope.tableAlias.empty() ? scope.model->tableName : std::string_view{scope.tableAlias};
                result += sqlConcat(sqlQualified(policy, alias, column.name), " = ",
                                    sqlQualified(policy, root, sqlConcat(relation.name, "_", column.name)));
            }
        if (scope.allowCollections)
            result += ' ';
    }
    if (scope.allowCollections && !result.empty())
        result.pop_back();
    return result;
}

template <typename Policy>
[[nodiscard]] constexpr auto emitSqlPredicate(const SqlQueryView& query, std::size_t index, const SqlRenderScope& scope,
                                              const Policy& policy) -> std::string;
template <typename Policy>
[[nodiscard]] constexpr auto emitSqlCollection(const SqlQueryView& query, const SqlNode& node,
                                               const SqlRenderScope& scope, const Policy& policy) -> std::string
{
    if (!scope.allowCollections)
        throw std::invalid_argument{"Nested collection predicates are not supported"};
    const auto* relation = scope.model.findRelation(node.relation);
    if (relation == nullptr || relation->kind == model::RelationKind::ToOne)
        throw std::invalid_argument{sqlConcat("Unknown collection relation: ", node.relation)};
    if (node.operation != static_cast<unsigned>(query::detail::CollectionOperator::Exists) && node.left == noSqlNode)
        throw std::invalid_argument{"Collection any/none requires an element predicate"};
    const auto ownerKeys = sqlPrimaryKey(scope.model);
    const auto target = scope.model.resolveTarget(*relation);
    if (!target)
        throw std::logic_error{"Collection relation target is not available in the schema"};
    const auto targetKeys = sqlPrimaryKey(target);
    const auto outer = scope.tableAlias.empty() ? std::string{scope.model->tableName} : scope.tableAlias;
    std::vector<std::string> reserved{outer};
    for (const auto& column : target->columns)
        if (column.kind == model::FieldKind::ToOne)
            reserved.emplace_back(column.name);
    const auto targetAlias = uniqueSqlAlias("orm_relation_target", reserved);
    reserved.push_back(targetAlias);
    const auto junctionAlias = uniqueSqlAlias("orm_relation_junction", reserved);
    reserved.push_back(junctionAlias);
    std::vector<std::string> assigned{outer, targetAlias, junctionAlias};
    SqlRenderScope nested{target, true, false, false, targetAlias, {}};
    for (const auto& column : target->columns)
        if (column.kind == model::FieldKind::ToOne)
        {
            const auto alias =
                hasSqlAlias(assigned, column.name) ? uniqueSqlAlias(column.name, reserved) : std::string{column.name};
            nested.relationAliases.emplace_back(column.name, alias);
            assigned.push_back(alias);
            reserved.push_back(alias);
        }
    std::string from;
    std::string predicate;
    if (relation->kind == model::RelationKind::OneToMany)
    {
        const auto* mapped = target.findRelation(relation->mappedBy);
        if (mapped == nullptr || mapped->kind != model::RelationKind::ToOne)
            throw std::invalid_argument{sqlConcat("Invalid OneToMany mappedBy relation: ", relation->fieldName)};
        for (const auto* key : ownerKeys)
        {
            if (!predicate.empty())
                predicate += " AND ";
            predicate += sqlConcat(sqlQualified(policy, targetAlias, sqlConcat(mapped->columnName, "_", key->name)),
                                   " = ", sqlQualified(policy, outer, key->name));
        }
        from = sqlConcat(policy.quoteIdentifier(target->tableName), " AS ", policy.quoteIdentifier(targetAlias),
                         emitSqlJoins(nested, policy));
    }
    else
    {
        const auto junction = scope.model.resolveJunction(*relation);
        if (!junction.isConfigured())
            throw std::invalid_argument{
                sqlConcat("ManyToMany relation has no junction mapping: ", relation->fieldName)};
        if (junction.ownerColumns.size() != ownerKeys.size() || junction.targetColumns.size() != targetKeys.size())
            throw std::invalid_argument{
                sqlConcat("Junction columns do not match relation endpoint keys: ", junction.tableName)};
        for (std::size_t i = 0; i < ownerKeys.size(); ++i)
        {
            if (!predicate.empty())
                predicate += " AND ";
            predicate += sqlConcat(sqlQualified(policy, junctionAlias, junction.ownerColumns[i]), " = ",
                                   sqlQualified(policy, outer, ownerKeys[i]->name));
        }
        std::string targetJoin;
        for (std::size_t i = 0; i < targetKeys.size(); ++i)
        {
            if (!targetJoin.empty())
                targetJoin += " AND ";
            targetJoin += sqlConcat(sqlQualified(policy, targetAlias, targetKeys[i]->name), " = ",
                                    sqlQualified(policy, junctionAlias, junction.targetColumns[i]));
        }
        from = sqlConcat(policy.quoteIdentifier(junction.tableName), " AS ", policy.quoteIdentifier(junctionAlias),
                         " JOIN ", policy.quoteIdentifier(target->tableName), " AS ",
                         policy.quoteIdentifier(targetAlias), " ON ", targetJoin, emitSqlJoins(nested, policy));
    }
    if (node.left != noSqlNode)
        predicate += sqlConcat(" AND ", emitSqlPredicate(query, node.left, nested, policy));
    const auto exists = sqlConcat("EXISTS (SELECT 1 FROM ", from, " WHERE ", predicate, ")");
    return node.operation == static_cast<unsigned>(query::detail::CollectionOperator::None) ?
               sqlConcat("NOT (", exists, ")") :
               exists;
}
template <typename Policy>
[[nodiscard]] constexpr auto emitSqlPredicate(const SqlQueryView& query, std::size_t index, const SqlRenderScope& scope,
                                              const Policy& policy) -> std::string
{
    const auto& node = query.nodes[index];
    auto bind = [&](std::size_t offset)
    { return policy.bindMarker(parameterName(query.bindings[node.firstParameter + offset])); };
    switch (node.kind)
    {
    case SqlNodeKind::Comparison:
        if (node.source.isAggregate && node.operation >= static_cast<unsigned>(query::detail::ComparisonOperator::Like))
            throw std::invalid_argument{"Unsupported aggregate comparison operator"};
        return sqlConcat(emitSqlSource(node.source, scope, policy), " ", sqlComparison(node.operation), " ", bind(0));
    case SqlNodeKind::Null:
        return sqlConcat(emitSqlColumn(node.source, scope, policy),
                         node.operation == static_cast<unsigned>(query::detail::NullOperator::IsNull) ? " IS NULL" :
                                                                                                        " IS NOT NULL");
    case SqlNodeKind::List:
    {
        const auto in = node.operation == static_cast<unsigned>(query::detail::ListOperator::In);
        if (node.parameterCount == 0)
            return std::string{in ? "(1 = 0)" : "(1 = 1)"};
        auto result = sqlConcat(emitSqlColumn(node.source, scope, policy), in ? " IN (" : " NOT IN (");
        for (std::size_t i = 0; i < node.parameterCount; ++i)
        {
            if (i != 0)
                result += ", ";
            result += bind(i);
        }
        result += ')';
        return result;
    }
    case SqlNodeKind::Between:
        return sqlConcat(emitSqlColumn(node.source, scope, policy),
                         node.operation == static_cast<unsigned>(query::detail::BetweenOperator::Between) ?
                             " BETWEEN " :
                             " NOT BETWEEN ",
                         bind(0), " AND ", bind(1));
    case SqlNodeKind::And:
    case SqlNodeKind::Or:
        return sqlConcat("(", emitSqlPredicate(query, node.left, scope, policy),
                         node.kind == SqlNodeKind::And ? " AND " : " OR ",
                         emitSqlPredicate(query, node.right, scope, policy), ")");
    case SqlNodeKind::Not:
        return sqlConcat("(NOT (", emitSqlPredicate(query, node.left, scope, policy), "))");
    case SqlNodeKind::Raw:
        return std::string{node.rawSql};
    case SqlNodeKind::Collection:
        return emitSqlCollection(query, node, scope, policy);
    case SqlNodeKind::Invalid:
        break;
    }
    throw std::logic_error{"Unknown SQL predicate node"};
}
template <typename Policy>
[[nodiscard]] constexpr auto emitSqlFullFields(const SqlRenderScope& scope, const Policy& policy) -> std::string
{
    std::string result;
    auto append = [&](std::string expression, std::string alias)
    {
        if (!result.empty())
            result += ", ";
        result += sqlConcat(expression, " AS ", policy.quoteIdentifier(alias));
    };
    for (const auto& column : scope.model->columns)
    {
        if (column.kind == model::FieldKind::Scalar)
            append(sqlQualified(policy, scope.model->tableName, column.name),
                   sqlConcat(scope.model->tableName, "_", column.name));
        else
        {
            const auto target = requireSqlTarget(scope.model, column);
            for (const auto& child : target->columns)
                if (scope.shouldJoin)
                    append(sqlQualified(policy, column.name, child.name), sqlConcat(column.name, "_", child.name));
                else if (child.isPrimaryKey)
                    append(sqlQualified(policy, scope.model->tableName, sqlConcat(column.name, "_", child.name)),
                           sqlConcat(scope.model->tableName, "_", column.name, "_", child.name));
        }
    }
    return result;
}
template <typename Policy>
[[nodiscard]] constexpr auto emitSql(const SqlQueryView& query, model::ModelView model,
                                     const Policy& policy) -> std::string
{
    SqlRenderScope scope{model, query.shouldJoin, query.operation != SqlOperation::Select, true, {}, {}};
    std::string result;
    if (query.operation == SqlOperation::Select)
    {
        result = query.isDistinct ? "SELECT DISTINCT " : "SELECT ";
        if (query.projections.empty())
            result += emitSqlFullFields(scope, policy);
        else
            for (std::size_t i = 0; i < query.projections.size(); ++i)
            {
                if (i != 0)
                    result += ", ";
                result += sqlConcat(emitSqlSource(query.projections[i].source, scope, policy, true), " AS ",
                                    policy.quoteIdentifier(query.projections[i].alias));
            }
        result += sqlConcat(" FROM ", policy.quoteIdentifier(model->tableName));
        if (query.shouldJoin)
            result += emitSqlJoins(scope, policy);
    }
    else if (query.operation == SqlOperation::Update)
    {
        if (query.assignments.empty())
            throw std::invalid_argument{"UPDATE requires at least one assignment"};
        if (query.predicate == noSqlNode)
            throw std::invalid_argument{"UPDATE requires a WHERE predicate"};
        result = sqlConcat("UPDATE ", policy.quoteIdentifier(model->tableName), " SET ");
        for (std::size_t i = 0; i < query.assignments.size(); ++i)
        {
            if (i != 0)
                result += ", ";
            const auto& assignment = query.assignments[i];
            result += sqlConcat(emitSqlColumn(assignment.source, scope, policy, false), " = ",
                                policy.bindMarker(parameterName(query.bindings[assignment.parameter])));
        }
    }
    else
        result = sqlConcat("DELETE FROM ", policy.quoteIdentifier(model->tableName));
    if (query.predicate != noSqlNode)
        result += sqlConcat(" WHERE ", emitSqlPredicate(query, query.predicate, scope, policy));
    if (!query.groups.empty())
    {
        result += " GROUP BY ";
        for (std::size_t i = 0; i < query.groups.size(); ++i)
        {
            if (i != 0)
                result += ", ";
            result += emitSqlColumn(query.groups[i], scope, policy);
        }
    }
    if (query.having != noSqlNode)
        result += sqlConcat(" HAVING ", emitSqlPredicate(query, query.having, scope, policy));
    if (!query.orders.empty())
    {
        result += " ORDER BY ";
        for (std::size_t i = 0; i < query.orders.size(); ++i)
        {
            if (i != 0)
                result += ", ";
            const auto& order = query.orders[i];
            result += order.isRaw ? std::string{order.rawSql} :
                                    sqlConcat(emitSqlColumn(order.source, scope, policy),
                                              order.direction == query::detail::OrderDirection::Asc ? " ASC" : " DESC");
        }
    }
    result += policy.pagination(query);
    result += ';';
    return result;
}
} // namespace detail
} // namespace orm::db

// database/TypeTranslator.hpp
namespace orm::db
{
export {

class TypeTranslator
{
public:
    virtual ~TypeTranslator() = default;

    [[nodiscard]] virtual auto toSqlType(model::ColumnType type) const -> std::string = 0;
};

}
} // namespace orm::db

// database/postgresql/PostgresqlDialect.hpp
namespace orm::db::postgresql
{
export {

class PostgresqlDialect final : public SqlDialect
{
public:
    [[nodiscard]] auto quoteIdentifier(std::string_view identifier) const -> std::string override;
    [[nodiscard]] auto bindMarker(std::string_view logicalName) const -> std::string override;
    [[nodiscard]] auto toSqlType(model::ColumnType type) const -> std::string override;
    [[nodiscard]] auto renderCreateTablePrefix(std::string_view tableName,
                                               bool ifNotExists) const -> std::string override;
    [[nodiscard]] auto renderDropTable(std::string_view tableName, bool ifExists) const -> std::string override;
    [[nodiscard]] auto renderAutoIncrementPrimaryKey(std::string_view columnName) const -> std::string override;
    [[nodiscard]] auto renderPagination(const PaginationSpec& pagination) const -> std::string override;
    [[nodiscard]] auto renderInsertIfAbsent(const InsertIfAbsentSpec& insert) const -> std::string override;
    [[nodiscard]] auto renderAggregateResult(std::string_view expression,
                                             bool preserveExactNumeric) const -> std::string override;
};

}
} // namespace orm::db::postgresql

// database/postgresql/PostgresqlBackend.hpp
namespace orm::db::postgresql
{
export {

class PostgresqlBackend final : public BackendProvider
{
public:
    PostgresqlBackend();
    ~PostgresqlBackend() override;

    [[nodiscard]] auto type() const noexcept -> BackendType override;
    [[nodiscard]] auto acceptsConnectionString(std::string_view connectionString) const noexcept -> bool override;
    [[nodiscard]] auto capabilities() const noexcept -> const BackendCapabilities& override;
    [[nodiscard]] auto dialect() const noexcept -> const SqlDialect& override;
    [[nodiscard]] auto runtime() const noexcept -> const BackendRuntime& override;
    [[nodiscard]] auto commandGenerator() const noexcept -> const CommandGenerator& override;
    [[nodiscard]] auto compiledSqlFlavor() const noexcept -> CompiledSqlFlavor override
    {
        return CompiledSqlFlavor::PostgreSQL;
    }

private:
    BackendCapabilities backendCapabilities;
    PostgresqlDialect postgresqlDialect;
    std::unique_ptr<BackendRuntime> backendRuntime;
    std::unique_ptr<CommandGenerator> postgresqlCommandGenerator;
};

}
} // namespace orm::db::postgresql

// database/sqlite/SqliteDialect.hpp
namespace orm::db::sqlite
{
export {

class SqliteDialect final : public SqlDialect
{
public:
    [[nodiscard]] auto quoteIdentifier(std::string_view identifier) const -> std::string override;
    [[nodiscard]] auto bindMarker(std::string_view logicalName) const -> std::string override;
    [[nodiscard]] auto toSqlType(model::ColumnType type) const -> std::string override;
    [[nodiscard]] auto renderCreateTablePrefix(std::string_view tableName,
                                               bool ifNotExists) const -> std::string override;
    [[nodiscard]] auto renderDropTable(std::string_view tableName, bool ifExists) const -> std::string override;
    [[nodiscard]] auto renderAutoIncrementPrimaryKey(std::string_view columnName) const -> std::string override;
    [[nodiscard]] auto renderPagination(const PaginationSpec& pagination) const -> std::string override;
    [[nodiscard]] auto renderInsertIfAbsent(const InsertIfAbsentSpec& insert) const -> std::string override;
};

}
} // namespace orm::db::sqlite

// database/sqlite/SqliteBackend.hpp
namespace orm::db::sqlite
{
export {

class SqliteBackend final : public BackendProvider
{
public:
    SqliteBackend();
    ~SqliteBackend() override;

    [[nodiscard]] auto type() const noexcept -> BackendType override;
    [[nodiscard]] auto acceptsConnectionString(std::string_view connectionString) const noexcept -> bool override;
    [[nodiscard]] auto capabilities() const noexcept -> const BackendCapabilities& override;
    [[nodiscard]] auto dialect() const noexcept -> const SqlDialect& override;
    [[nodiscard]] auto runtime() const noexcept -> const BackendRuntime& override;
    [[nodiscard]] auto commandGenerator() const noexcept -> const CommandGenerator& override;
    [[nodiscard]] auto compiledSqlFlavor() const noexcept -> CompiledSqlFlavor override
    {
        return CompiledSqlFlavor::SQLite;
    }

private:
    BackendCapabilities backendCapabilities;
    SqliteDialect sqliteDialect;
    std::unique_ptr<BackendRuntime> backendRuntime;
    std::unique_ptr<CommandGenerator> sqliteCommandGenerator;
};

}
} // namespace orm::db::sqlite

// query/CompiledSql.hpp
namespace orm::query::detail
{
namespace compiled
{
using namespace orm::db::detail;

template <typename Tuple, typename Callback>
constexpr auto visitTypes(Callback&& callback) -> void
{
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        (callback.template operator()<std::tuple_element_t<I, Tuple>>(), ...);
    }(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
}
template <typename E>
consteval auto source() -> SqlSource
{
    SqlSource result;
    if constexpr (E::kind == ExprKind::Aggregate)
    {
        if constexpr (!std::same_as<typename E::Source, void>)
            result = source<typename E::Source>();
        result.isAggregate = true;
        result.function = E::function;
    }
    else
    {
        result.pathSize = E::pathParts.size();
        for (std::size_t i = 0; i < result.pathSize; ++i)
            result.pathParts[i] = E::pathParts[i];
    }
    return result;
}
template <typename Value>
constexpr auto addBinding(SqlProgram& program) -> std::size_t
{
    const auto index = program.bindings.size();
    program.bindings.push_back({model::LogicalTypeTraits<Value>::value, index, {}});
    return index;
}
template <typename E>
constexpr auto appendExpression(SqlProgram& program) -> std::size_t
{
    SqlNode node;
    if constexpr (E::kind == ExprKind::Comparison || E::kind == ExprKind::Null || E::kind == ExprKind::List ||
                  E::kind == ExprKind::Between)
    {
        node.source = source<typename E::Source>();
        node.operation = static_cast<unsigned>(E::operation);
        node.firstParameter = program.bindings.size();
        if constexpr (E::kind == ExprKind::Comparison)
        {
            node.kind = SqlNodeKind::Comparison;
            node.parameterCount = 1;
        }
        else if constexpr (E::kind == ExprKind::Null)
            node.kind = SqlNodeKind::Null;
        else if constexpr (E::kind == ExprKind::Between)
        {
            node.kind = SqlNodeKind::Between;
            node.parameterCount = 2;
        }
        else
        {
            node.kind = SqlNodeKind::List;
            node.parameterCount = E::arity;
        }
        if constexpr (E::kind != ExprKind::Null)
            for (std::size_t i = 0; i < node.parameterCount; ++i)
                (void)addBinding<typename E::Source::Value>(program);
    }
    else if constexpr (E::kind == ExprKind::Logical)
    {
        node.kind = E::operation == LogicalOperator::And ? SqlNodeKind::And : SqlNodeKind::Or;
        node.left = appendExpression<typename E::Left>(program);
        node.right = appendExpression<typename E::Right>(program);
    }
    else if constexpr (E::kind == ExprKind::Not)
    {
        node.kind = SqlNodeKind::Not;
        node.left = appendExpression<typename E::Child>(program);
    }
    else if constexpr (E::kind == ExprKind::Collection)
    {
        node.kind = SqlNodeKind::Collection;
        node.operation = static_cast<unsigned>(E::operation);
        node.relation = E::relationName;
        if constexpr (!std::same_as<typename E::Child, void>)
            node.left = appendExpression<typename E::Child>(program);
    }
    else
        static_assert(sizeof(E) == 0, "ORM_QUERY_STATIC_SQL: expression is not eligible for compiled SQL");
    const auto index = program.nodes.size();
    program.nodes.push_back(node);
    return index;
}
template <typename Tuple>
struct TupleEligible;
template <typename... Items>
struct TupleEligible<std::tuple<Items...>> : std::bool_constant<(staticSqlEligible<Items> && ...)>
{
};
template <typename P>
inline constexpr bool eligible =
    (!P::hasPredicate || staticSqlEligible<typename P::Predicate>) &&
    (!P::hasHaving || staticSqlEligible<typename P::Having>) && TupleEligible<typename P::Orders>::value &&
    TupleEligible<typename P::Projections>::value;
template <typename P>
constexpr auto program() -> SqlProgram
{
    SqlProgram result;
    result.operation = static_cast<SqlOperation>(P::operation);
    result.shouldJoin = P::operation == PlanOperation::Select && P::shouldJoin;
    result.isDistinct = P::isDistinct;
    visitTypes<typename P::Assignments>(
        [&]<typename A>() {
            result.assignments.push_back({source<typename A::Source>(), addBinding<typename A::Source::Value>(result)});
        });
    if constexpr (P::hasPredicate)
        result.predicate = appendExpression<typename P::Predicate>(result);
    visitTypes<typename P::Groups>([&]<typename C>() { result.groups.push_back(source<C>()); });
    if constexpr (P::hasHaving)
        result.having = appendExpression<typename P::Having>(result);
    visitTypes<typename P::Orders>(
        [&]<typename O>() { result.orders.push_back({source<typename O::Source>(), O::direction, false, {}}); });
    visitTypes<typename P::Projections>(
        [&]<typename Projection>()
        { result.projections.push_back({source<typename Projection::Source>(), Projection::alias.view()}); });
    visitTypes<typename P::Includes>(
        [&]<typename I>() { result.includes.push_back(model::detail::reflectedMemberNameStorage<I::member>.view()); });
    result.hasLimit = P::hasLimit;
    result.hasOffset = P::hasOffset;
    result.boundPagination = true;
    if constexpr (P::hasLimit)
        result.limitParameter = addBinding<std::size_t>(result);
    if constexpr (P::hasOffset)
        result.offsetParameter = addBinding<std::size_t>(result);
    return result;
}

[[nodiscard]] consteval auto validSource(const SqlSource& value, model::ModelView model, bool write, bool joins) -> bool
{
    if (value.isAggregate && value.function == AggregateFunction::CountAll)
        return true;
    if (value.pathSize == 0 || value.pathSize > 2)
        return false;
    const auto* column = model.findColumn(value.pathParts[0]);
    if (column == nullptr)
        return false;
    if (value.pathSize == 1)
        return column->kind == model::FieldKind::Scalar && column->type.has_value();
    if (column->kind != model::FieldKind::ToOne)
        return false;
    const auto target = model.resolveTarget(*column);
    if (!target)
        return false;
    const auto* child = target.findColumn(value.pathParts[1]);
    return child != nullptr && child->kind == model::FieldKind::Scalar && child->type.has_value() &&
           (!(write || !joins) || child->isPrimaryKey);
}
[[nodiscard]] consteval auto validKeys(model::ModelView model) -> bool
{
    bool found = false;
    for (const auto& column : model->columns)
        if (column.isPrimaryKey)
        {
            if (column.kind != model::FieldKind::Scalar)
                return false;
            found = true;
        }
    return found;
}
[[nodiscard]] consteval auto validNode(const SqlQueryView& view, std::size_t index, model::ModelView model, bool write,
                                       bool joins, bool collectionAllowed = true) -> bool
{
    if (index == noSqlNode)
        return true;
    const auto& node = view.nodes[index];
    if (node.kind == SqlNodeKind::And || node.kind == SqlNodeKind::Or)
        return validNode(view, node.left, model, write, joins, collectionAllowed) &&
               validNode(view, node.right, model, write, joins, collectionAllowed);
    if (node.kind == SqlNodeKind::Not)
        return validNode(view, node.left, model, write, joins, collectionAllowed);
    if (node.kind != SqlNodeKind::Collection)
        return validSource(node.source, model, write, joins);
    if (!collectionAllowed || !validKeys(model))
        return false;
    const auto* relation = model.findRelation(node.relation);
    if (relation == nullptr || relation->kind == model::RelationKind::ToOne)
        return false;
    const auto target = model.resolveTarget(*relation);
    if (!target || !validKeys(target))
        return false;
    if (relation->kind == model::RelationKind::OneToMany)
    {
        const auto* mapped = target.findRelation(relation->mappedBy);
        if (mapped == nullptr || mapped->kind != model::RelationKind::ToOne)
            return false;
    }
    else
    {
        const auto junction = model.resolveJunction(*relation);
        if (!junction.isConfigured() || junction.ownerColumns.size() != model.primaryKeySize() ||
            junction.targetColumns.size() != target.primaryKeySize())
            return false;
    }
    if (node.operation != static_cast<unsigned>(CollectionOperator::Exists) && node.left == noSqlNode)
        return false;
    for (const auto& column : target->columns)
        if (column.kind == model::FieldKind::ToOne)
        {
            const auto child = target.resolveTarget(column);
            if (!child || !validKeys(child))
                return false;
        }
    return validNode(view, node.left, target, false, true, false);
}
template <db::CompiledSqlFlavor Flavor>
struct CheckingPolicy : db::detail::StaticSqlPolicy<Flavor>
{
    bool* valid;
    [[nodiscard]] constexpr auto quoteIdentifier(std::string_view name) const -> std::string
    {
        if (name.empty() || name.find('\0') != std::string_view::npos ||
            (Flavor == db::CompiledSqlFlavor::PostgreSQL && name.size() > 63))
            *valid = false;
        std::string result{"\""};
        for (const auto character : name)
        {
            if (character == '"')
                result += '"';
            result += character;
        }
        result += '"';
        return result;
    }
};
template <typename Schema, typename P, db::CompiledSqlFlavor Flavor>
consteval auto valid() -> bool
{
    const auto data = program<P>();
    const auto view = data.view();
    constexpr auto model = model::modelView<Schema, typename P::Model>();
    const auto write = P::operation != PlanOperation::Select;
    if constexpr (P::operation == PlanOperation::Update)
        if (data.assignments.empty() || view.predicate == noSqlNode)
            return false;
    if constexpr (P::operation == PlanOperation::Remove)
        if (view.predicate == noSqlNode)
            return false;
    if (!validNode(view, view.predicate, model, write, view.shouldJoin) ||
        !validNode(view, view.having, model, false, view.shouldJoin))
        return false;
    for (const auto& item : view.groups)
        if (!validSource(item, model, false, view.shouldJoin))
            return false;
    for (const auto& item : view.orders)
        if (!validSource(item.source, model, false, view.shouldJoin))
            return false;
    for (const auto& item : view.projections)
        if (!validSource(item.source, model, false, view.shouldJoin))
            return false;
    for (const auto& item : view.assignments)
        if (!validSource(item.source, model, true, false))
            return false;
    for (const auto& column : model->columns)
        if (column.kind == model::FieldKind::ToOne && !model.resolveTarget(column))
            return false;
    bool result = true;
    (void)emitSql(view, model, CheckingPolicy<Flavor>{{}, &result});
    return result;
}

template <std::size_t Nodes, std::size_t Orders, std::size_t Groups, std::size_t Projections, std::size_t Assignments,
          std::size_t Includes, std::size_t Bindings>
struct FrozenProgram
{
    std::array<SqlNode, Nodes> nodes{};
    std::array<SqlOrder, Orders> orders{};
    std::array<SqlSource, Groups> groups{};
    std::array<SqlProjection, Projections> projections{};
    std::array<SqlAssignment, Assignments> assignments{};
    std::array<std::string_view, Includes> includes{};
    std::array<SqlBindingDescriptor, Bindings> bindings{};
    SqlOperation operation{};
    std::size_t predicate = noSqlNode;
    std::size_t having = noSqlNode;
    bool isDistinct{};
    bool shouldJoin = true;
    bool hasLimit{};
    bool hasOffset{};
    std::size_t limitParameter{};
    std::size_t offsetParameter{};
    [[nodiscard]] constexpr auto view() const -> SqlQueryView
    {
        return {operation,   nodes,          predicate,       having,     orders,     groups,   projections,
                assignments, includes,       bindings,        isDistinct, shouldJoin, hasLimit, hasOffset,
                true,        limitParameter, offsetParameter, 0,          0};
    }
};
template <typename P>
consteval auto freeze()
{
    constexpr auto sizes = []() consteval
    {
        auto data = program<P>();
        return std::array{data.nodes.size(),       data.orders.size(),   data.groups.size(),  data.projections.size(),
                          data.assignments.size(), data.includes.size(), data.bindings.size()};
    }();
    FrozenProgram<sizes[0], sizes[1], sizes[2], sizes[3], sizes[4], sizes[5], sizes[6]> result;
    const auto data = program<P>();
    std::ranges::copy(data.nodes, result.nodes.begin());
    std::ranges::copy(data.orders, result.orders.begin());
    std::ranges::copy(data.groups, result.groups.begin());
    std::ranges::copy(data.projections, result.projections.begin());
    std::ranges::copy(data.assignments, result.assignments.begin());
    std::ranges::copy(data.includes, result.includes.begin());
    std::ranges::copy(data.bindings, result.bindings.begin());
    result.operation = data.operation;
    result.predicate = data.predicate;
    result.having = data.having;
    result.isDistinct = data.isDistinct;
    result.shouldJoin = data.shouldJoin;
    result.hasLimit = data.hasLimit;
    result.hasOffset = data.hasOffset;
    result.limitParameter = data.limitParameter;
    result.offsetParameter = data.offsetParameter;
    return result;
}
template <typename Frozen, std::size_t Length>
struct CompiledStatement
{
    Frozen program{};
    std::array<char, Length + 1> sql{};
    bool valid{};
    [[nodiscard]] constexpr auto view() const -> std::string_view
    {
        return {sql.data(), Length};
    }
};
template <typename Schema, typename P, db::CompiledSqlFlavor Flavor>
consteval auto compile()
{
    constexpr bool accepted = valid<Schema, P, Flavor>();
    constexpr auto length = []() consteval
    {
        if constexpr (accepted)
        {
            auto data = program<P>();
            return emitSql(data.view(), model::modelView<Schema, typename P::Model>(),
                           db::detail::StaticSqlPolicy<Flavor>{})
                .size();
        }
        else
            return std::size_t{};
    }();
    using Frozen = decltype(freeze<P>());
    CompiledStatement<Frozen, length> result;
    result.program = freeze<P>();
    result.valid = accepted;
    if constexpr (accepted)
    {
        const auto value = emitSql(result.program.view(), model::modelView<Schema, typename P::Model>(),
                                   db::detail::StaticSqlPolicy<Flavor>{});
        std::ranges::copy(value, result.sql.begin());
    }
    return result;
}
} // namespace compiled

template <typename P>
inline constexpr bool compiledSqlEligible = compiled::eligible<P>;
template <typename Schema, typename P, db::CompiledSqlFlavor Flavor>
    requires compiledSqlEligible<P>
inline constexpr auto compiledStatement = compiled::compile<Schema, P, Flavor>();

template <typename P, typename Args>
    requires compiledSqlEligible<P>
auto collectParameters(const P& plan, const Args& args) -> std::vector<db::StatementParameter>
{
    std::vector<db::StatementParameter> result;
    const auto& state = PlanAccess::get(plan);
    auto append = [&](std::optional<QueryValue> value, model::ColumnType logicalType)
    {
        const auto nullType = value.has_value() ? std::optional<model::ColumnType>{} : std::optional{logicalType};
        result.push_back({.name = db::detail::parameterName({logicalType, result.size(), {}}),
                          .value = std::move(value),
                          .nullType = nullType});
    };
    std::apply(
        [&](const auto&... assignment)
        {
            [[maybe_unused]] auto add = [&]<typename A>(const A& item)
            {
                using C = typename A::Source;
                const auto& value = resolveValue(item.value, args);
                constexpr auto type = model::LogicalTypeTraits<typename C::Value>::value;
                using V = std::remove_cvref_t<decltype(value)>;
                if constexpr (std::same_as<V, std::nullopt_t> || std::same_as<V, std::nullptr_t>)
                    append(std::nullopt, type);
                else if constexpr (model::isNullable<V>)
                {
                    if (value.has_value())
                        append(typedValue<typename C::Value>(*value), type);
                    else
                        append(std::nullopt, type);
                }
                else
                    append(typedValue<typename C::Value>(value), type);
            };
            (add(assignment), ...);
        },
        std::get<5>(state));
    auto addValue = [&](QueryValue value)
    {
        const auto type = value.getLogicalType();
        append(std::move(value), type);
    };
    if constexpr (P::hasPredicate)
        visitValues(std::get<0>(state), args, addValue);
    if constexpr (P::hasHaving)
        visitValues(std::get<3>(state), args, addValue);
    [[maybe_unused]] auto addPagination = [&](const auto& stored)
    {
        const auto value = static_cast<std::size_t>(resolveValue(stored, args));
        if (value > static_cast<std::size_t>(std::numeric_limits<long long>::max()))
            throw std::out_of_range{"Pagination exceeds the supported signed 64-bit range"};
        addValue(QueryValue{value});
    };
    if constexpr (P::hasLimit)
        addPagination(std::get<6>(state));
    if constexpr (P::hasOffset)
        addPagination(std::get<7>(state));
    return result;
}
} // namespace orm::query::detail

