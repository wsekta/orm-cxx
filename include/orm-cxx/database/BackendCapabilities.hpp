#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <vector>

#include "orm-cxx/model/ColumnType.hpp"

namespace orm::db
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
} // namespace orm::db
