module;

#include <algorithm>
#include <compare>
#include <cstddef>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "../../detail/SociTypes.inc"

export module orm:sql_contracts;

import :foundation;
import :model;
import :expressions;

// Runtime command inputs form the source-level backend extension contract.
export namespace orm::db
{
using SelectSpec = query::detail::SelectSpec;
using UpdateSpec = query::detail::UpdateSpec;
using Predicate = query::detail::Predicate;
}

export namespace orm::db::ast
{
using Column = orm::query::detail::Column;
using PredicateNode = orm::query::detail::PredicateNode;
using PredicateNodePtr = orm::query::detail::PredicateNodePtr;
using ComparisonOperator = orm::query::detail::ComparisonOperator;
using NullOperator = orm::query::detail::NullOperator;
using ListOperator = orm::query::detail::ListOperator;
using BetweenOperator = orm::query::detail::BetweenOperator;
using LogicalOperator = orm::query::detail::LogicalOperator;
using CollectionOperator = orm::query::detail::CollectionOperator;
using ComparisonExpression = orm::query::detail::ComparisonExpression;
using NullExpression = orm::query::detail::NullExpression;
using ListExpression = orm::query::detail::ListExpression;
using BetweenExpression = orm::query::detail::BetweenExpression;
using LogicalExpression = orm::query::detail::LogicalExpression;
using NotExpression = orm::query::detail::NotExpression;
using RawExpression = orm::query::detail::RawExpression;
using CollectionExpression = orm::query::detail::CollectionExpression;
using AggregateFunction = orm::query::detail::AggregateFunction;
using AggregateExpression = orm::query::detail::AggregateExpression;
using AggregatePredicate = orm::query::detail::AggregatePredicate;
using AggregatePredicateNode = orm::query::detail::AggregatePredicateNode;
using AggregatePredicateNodePtr = orm::query::detail::AggregatePredicateNodePtr;
using AggregateComparisonExpression = orm::query::detail::AggregateComparisonExpression;
using AggregateLogicalExpression = orm::query::detail::AggregateLogicalExpression;
using AggregateNotExpression = orm::query::detail::AggregateNotExpression;
using OrderDirection = orm::query::detail::OrderDirection;
using OrderBy = orm::query::detail::OrderBy;
using ProjectionSource = orm::query::detail::ProjectionSource;
using Projection = orm::query::detail::Projection;
using UpdateValue = orm::query::detail::UpdateValue;
using UpdateAssignment = orm::query::detail::UpdateAssignment;
}
namespace orm::db
{
export
{

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
namespace orm::db
{
export
{

    class BackendRuntime;
    class CommandGenerator;
    class SqlDialect;

    class BackendProvider
    {
    public:
        virtual ~BackendProvider() = default;

        [[nodiscard]] virtual auto type() const noexcept -> BackendType = 0;
        [[nodiscard]] virtual auto
        acceptsConnectionString(std::string_view connectionString) const noexcept -> bool = 0;
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
namespace orm::db
{
export
{

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
namespace orm::db
{
export
{

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
// Primary-key storage shared by SQL statements and model binding.
namespace orm::db::binding
{
using PrimaryKey = std::vector<query::QueryValue>;
}

export namespace orm::db
{
using PrimaryKey = binding::PrimaryKey;
}
namespace orm::db
{
export
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
namespace orm::db
{
export
{

    class TypeTranslator
    {
    public:
        virtual ~TypeTranslator() = default;

        [[nodiscard]] virtual auto toSqlType(model::ColumnType type) const -> std::string = 0;
    };
}
} // namespace orm::db
