module;

#include <cstddef>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "soci/soci.h"

module orm:internal;

import :foundation;
import :model;
import :expressions;
import :dynamic_query;
import :static_plan;
import :sql;
import :backend_relations;
import :database;
import :migrations_internal;

// database/DatabaseValidation.hpp
namespace orm::detail
{
// These validators are shared by Database's operations. Keeping them separate
// also lets tests exercise defensive checks with immutable malformed descriptors.
[[nodiscard]] auto hasOwningJunction(model::ModelView owner) -> bool;
[[nodiscard]] auto requireToOneTarget(model::ModelView owner, const model::ColumnView& column) -> model::ModelView;
[[nodiscard]] auto requireRelationTarget(model::ModelView owner,
                                         const model::RelationView& relation) -> model::ModelView;
[[nodiscard]] auto requireEndpointKeyColumns(model::ModelView model, const db::binding::PrimaryKey& key)
    -> std::vector<const model::ColumnView*>;
[[nodiscard]] auto owningJunctionRelations(model::ModelView owner) -> std::vector<const model::RelationView*>;
[[nodiscard]] auto requireOwningJunctionTarget(model::ModelView owner,
                                               const model::RelationView& relation) -> model::ModelView;
[[nodiscard]] auto requireSupportedRelatedTarget(model::ModelView owner, const model::ColumnView& column,
                                                 db::BackendType backendType,
                                                 std::string_view operation) -> model::ModelView;
[[nodiscard]] auto requireIncludedRelationTarget(model::ModelView owner, const model::RelationView& relation,
                                                 db::BackendType backendType) -> model::ModelView;
[[nodiscard]] auto requireCollectionTarget(model::ModelView owner, const model::RelationView& relation,
                                           model::TypeId expectedType) -> model::ModelView;
} // namespace orm::detail

// database/defaults/SqlRenderer.hpp
namespace orm::db::commands
{
enum class ColumnRenderMode
{
    Select,
    WritePredicate,
};

struct RenderContext
{
    model::ModelView model;
    const SqlDialect& dialect;
    bool shouldJoin = true;
    ColumnRenderMode columnRenderMode = ColumnRenderMode::Select;
    std::string tableAlias = {};
    std::unordered_map<std::string, std::string> relationAliases = {};
    bool allowCollectionPredicates = true;
    std::vector<StatementParameter> parameters = {};
    std::unordered_set<std::string> parameterNames = {};
    std::size_t nextParameterIndex = 0;
};

struct WriteColumn
{
    std::string sql;
    model::ColumnType type;
    bool isNotNull;
};

[[nodiscard]] auto renderColumn(const query::detail::Column& column, const RenderContext& context) -> std::string;
[[nodiscard]] auto renderWriteColumn(const query::detail::Column& column, model::ModelView model,
                                     const SqlDialect& dialect, bool qualifyWithTable) -> WriteColumn;
[[nodiscard]] auto renderWhere(const std::optional<query::detail::Predicate>& predicate,
                               RenderContext& context) -> std::string;
[[nodiscard]] auto renderWhere(const query::detail::Predicate& predicate, RenderContext& context) -> std::string;
[[nodiscard]] auto addAutomaticParameter(RenderContext& context, const query::QueryValue& value) -> std::string;
[[nodiscard]] auto addNullParameter(RenderContext& context, model::ColumnType type) -> std::string;
[[nodiscard]] auto renderSelectStatement(model::ModelView model, const query::detail::SelectSpec& spec,
                                         const SqlDialect& dialect) -> SelectStatement;
auto appendSelectPredicates(detail::SqlProgram& program, RenderContext& context,
                            const query::detail::SelectSpec& spec) -> void;
[[nodiscard]] auto renderUpdateStatement(model::ModelView model, const query::detail::UpdateSpec& spec,
                                         const SqlDialect& dialect) -> Statement;
[[nodiscard]] auto renderRemoveStatement(model::ModelView model, const query::detail::Predicate& predicate,
                                         const SqlDialect& dialect) -> Statement;
} // namespace orm::db::commands

// database/defaults/SqlAliases.hpp
namespace orm::db::aliases
{
[[nodiscard]] inline auto qualifiedIdentifier(const SqlDialect& dialect, std::string_view qualifier,
                                              std::string_view identifier) -> std::string
{
    return std::format("{}.{}", dialect.quoteIdentifier(qualifier), dialect.quoteIdentifier(identifier));
}

[[nodiscard]] inline auto modelColumn(std::string_view tableName, std::string_view columnName) -> std::string
{
    return std::format("{}_{}", tableName, columnName);
}

[[nodiscard]] inline auto joinedRelationColumn(std::string_view relationName,
                                               std::string_view columnName) -> std::string
{
    return std::format("{}_{}", relationName, columnName);
}

[[nodiscard]] inline auto unjoinedRelationColumn(std::string_view tableName, std::string_view relationName,
                                                 std::string_view columnName) -> std::string
{
    return std::format("{}_{}_{}", tableName, relationName, columnName);
}
} // namespace orm::db::aliases

namespace orm::db::mysql_family
{
[[nodiscard]] auto capabilities() -> BackendCapabilities;
[[nodiscard]] auto makeRuntime(BackendType type) -> std::unique_ptr<BackendRuntime>;
[[nodiscard]] auto sqlType(model::ColumnType type, std::optional<std::size_t> maxStringLength) -> std::string;
[[nodiscard]] auto renderInsert(const InsertIfAbsentSpec& insert) -> std::string;
struct StatementTokens
{
    std::vector<std::string> chunks;
    std::vector<std::string> names;
};
[[nodiscard]] auto tokenizeStatement(std::string_view sql) -> StatementTokens;
[[nodiscard]] auto isEmptyScript(std::string_view sql) -> bool;
}
