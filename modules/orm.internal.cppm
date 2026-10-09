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

module orm:internal;

import :foundation;
import :model;
import :expressions;
import :dynamic_query;
import :static_plan;
import :sql;
import :database;

// database/DatabaseValidation.hpp
namespace orm::detail
{
// These validators are shared by DatabaseCore's operations. Keeping them separate
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

// database/defaults/DefaultCommandGeneratorFactory.hpp
namespace orm::db
{
} // namespace orm::db

namespace orm::db::defaults
{
[[nodiscard]] auto makeDefaultCommandGenerator(const SqlDialect& dialect) -> std::unique_ptr<CommandGenerator>;
} // namespace orm::db::defaults

// database/defaults/DefaultCreateTableCommand.hpp
namespace orm::db::commands
{
class DefaultCreateTableCommand : public CreateTableCommand
{
public:
    explicit DefaultCreateTableCommand(const SqlDialect& dialect);

    [[nodiscard]] auto createTable(model::ModelView model) const -> std::string override;

private:
    struct ForeignKeyTarget
    {
        model::ColumnView relationColumn;
        model::ModelView target;
    };

    const SqlDialect& dialect;

    [[nodiscard]] auto addColumnsForForeignIds(model::ModelView target,
                                               const model::ColumnView& column) const -> std::string;
    [[nodiscard]] static auto hasAutoIncrementPrimaryKey(model::ModelView model) -> bool;
    [[nodiscard]] auto addForeignIds(const std::vector<ForeignKeyTarget>& foreignKeys) const -> std::string;
};
} // namespace orm::db::commands

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
[[nodiscard]] auto renderUpdateStatement(model::ModelView model, const query::detail::UpdateSpec& spec,
                                         const SqlDialect& dialect) -> Statement;
[[nodiscard]] auto renderRemoveStatement(model::ModelView model, const query::detail::Predicate& predicate,
                                         const SqlDialect& dialect) -> Statement;
} // namespace orm::db::commands

// database/defaults/DefaultDeleteCommand.hpp
namespace orm::db::commands
{
class DefaultDeleteCommand : public DeleteCommand
{
public:
    explicit DefaultDeleteCommand(const SqlDialect& dialect);

    [[nodiscard]] auto remove(model::ModelView model,
                              const query::detail::Predicate& predicate) const -> Statement override;

private:
    const SqlDialect& dialect;
};
} // namespace orm::db::commands

// database/defaults/DefaultDropTableCommand.hpp
namespace orm::db::commands
{
class DefaultDropTableCommand : public DropTableCommand
{
public:
    explicit DefaultDropTableCommand(const SqlDialect& dialect);

    [[nodiscard]] auto dropTable(model::ModelView model) const -> std::string override;

private:
    const SqlDialect& dialect;
};
} // namespace orm::db::commands

// database/defaults/DefaultInsertCommand.hpp
namespace orm::db::commands
{
class DefaultInsertCommand : public InsertCommand
{
public:
    explicit DefaultInsertCommand(const SqlDialect& dialect);

    [[nodiscard]] auto insert(model::ModelView model) const -> std::string override;

private:
    const SqlDialect& dialect;

    auto getInsertFields(const std::vector<std::string>& fieldNames) const -> std::string;
    auto getInsertValues(const std::vector<std::string>& fieldNames) const -> std::string;
    static auto getFieldsNames(model::ModelView model) -> std::vector<std::string>;
    static auto getForeignModelIdsNames(std::string_view foreignModelFieldName,
                                        model::ModelView target) -> std::vector<std::string>;
};
} // namespace orm::db::commands

// database/defaults/DefaultSelectCommand.hpp
namespace orm::db::commands
{
class DefaultSelectCommand : public SelectCommand
{
public:
    explicit DefaultSelectCommand(const SqlDialect& dialectInit);
    [[nodiscard]] auto select(model::ModelView model,
                              const query::detail::SelectSpec& spec) const -> SelectStatement override;

private:
    const SqlDialect& dialect;
};
}

// database/defaults/DefaultUpdateCommand.hpp
namespace orm::db::commands
{
class DefaultUpdateCommand : public UpdateCommand
{
public:
    explicit DefaultUpdateCommand(const SqlDialect& dialect);

    [[nodiscard]] auto update(model::ModelView model,
                              const query::detail::UpdateSpec& spec) const -> Statement override;

private:
    const SqlDialect& dialect;
};
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

// database/postgresql/PostgresqlTypeTranslator.hpp
namespace orm::db::postgresql
{
class PostgresqlTypeTranslator final : public TypeTranslator
{
public:
    [[nodiscard]] auto toSqlType(model::ColumnType type) const -> std::string override;
};
} // namespace orm::db::postgresql

// database/sqlite/SqliteTypeTranslator.hpp
namespace orm::db::sqlite
{
class SqliteTypeTranslator : public TypeTranslator
{
public:
    [[nodiscard]] auto toSqlType(model::ColumnType type) const -> std::string override;
};
}

