#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "model.hpp"
#include "orm-cxx/model/ColumnType.hpp"
#include "orm-cxx/query/QueryData.hpp"
#include "orm-cxx/utils/DisableExternalsWarning.hpp"

DISABLE_WARNING_PUSH
DISABLE_EXTERNAL_WARNINGS
#include <rfl.hpp>
DISABLE_WARNING_POP

namespace orm
{
class Database;

namespace detail
{
inline auto isSupportedProjectionResultType(model::ColumnType type) -> bool
{
    switch (type)
    {
    case model::ColumnType::Bool:
    case model::ColumnType::Char:
    case model::ColumnType::UnsignedChar:
    case model::ColumnType::Short:
    case model::ColumnType::UnsignedShort:
    case model::ColumnType::Int:
    case model::ColumnType::UnsignedInt:
    case model::ColumnType::LongLong:
    case model::ColumnType::UnsignedLongLong:
    case model::ColumnType::Float:
    case model::ColumnType::Double:
    case model::ColumnType::String:
        return true;
    case model::ColumnType::Uuid:
    case model::ColumnType::Unknown:
    case model::ColumnType::OneToOne:
        return false;
    }

    return false;
}

struct ProjectionResultField
{
    std::string name;
    model::ColumnType type;
};

inline auto validateProjectionAliasNames(const std::vector<query::Projection>& projections,
                                         const std::vector<ProjectionResultField>& fields) -> void
{
    if (projections.empty())
    {
        throw std::invalid_argument{"Projection query requires at least one projected field"};
    }

    std::unordered_set<std::string> resultFields;

    for (const auto& field : fields)
    {
        if (not isSupportedProjectionResultType(field.type))
        {
            throw std::invalid_argument{"Unsupported projection result field type: " + field.name};
        }

        resultFields.insert(field.name);
    }

    std::unordered_set<std::string> projectedFields;

    for (const auto& projection : projections)
    {
        if (projection.resultField.empty())
        {
            throw std::invalid_argument{"Projection alias must not be empty"};
        }

        if (not resultFields.contains(projection.resultField))
        {
            throw std::invalid_argument{"Projection alias does not match a result field: " + projection.resultField};
        }

        if (not projectedFields.insert(projection.resultField).second)
        {
            throw std::invalid_argument{"Duplicate projection alias: " + projection.resultField};
        }
    }

    for (const auto& resultField : resultFields)
    {
        if (not projectedFields.contains(resultField))
        {
            throw std::invalid_argument{"Missing projection alias for result field: " + resultField};
        }
    }
}

template <typename Result>
auto validateProjectionAliases(const std::vector<query::Projection>& projections) -> void
{
    const auto fields = rfl::fields<Result>();
    std::vector<ProjectionResultField> resultFields;

    for (const auto& field : fields)
    {
        const auto [type, isNotNull] = model::toColumnType(field.type());

        (void)isNotNull;

        resultFields.push_back({std::string{field.name()}, type});
    }

    validateProjectionAliasNames(projections, resultFields);
}
} // namespace detail

/**
 * @brief A select query that returns a user-defined DTO instead of the full source model.
 *
 * @tparam Source The mapped ORM model used in FROM, WHERE, ORDER BY, and projected columns.
 * @tparam Result The flat DTO hydrated from projected aliases.
 */
template <typename Source, typename Result>
class ProjectionQuery
{
public:
    ProjectionQuery() : data{.modelInfo = Model<Source>().getModelInfo()} {}

    template <typename... Projections>
    auto project(Projections... projections) -> ProjectionQuery<Source, Result>&
    {
        data.projections = {std::move(projections)...};
        detail::validateProjectionAliases<Result>(data.projections);

        return *this;
    }

    auto where(const query::Predicate& predicate) -> ProjectionQuery<Source, Result>&
    {
        data.predicate = predicate;

        return *this;
    }

    auto andWhere(const query::Predicate& predicate) -> ProjectionQuery<Source, Result>&
    {
        data.predicate = data.predicate.has_value() ? data.predicate.value() && predicate : predicate;

        return *this;
    }

    auto orWhere(const query::Predicate& predicate) -> ProjectionQuery<Source, Result>&
    {
        data.predicate = data.predicate.has_value() ? data.predicate.value() || predicate : predicate;

        return *this;
    }

    template <typename... Orders>
    auto orderBy(Orders... orders) -> ProjectionQuery<Source, Result>&
    {
        data.orderBy = {std::move(orders)...};

        return *this;
    }

    template <typename... Columns>
    auto groupBy(Columns... columns) -> ProjectionQuery<Source, Result>&
    {
        data.groupBy = {std::move(columns)...};

        return *this;
    }

    auto having(const query::AggregatePredicate& predicate) -> ProjectionQuery<Source, Result>&
    {
        data.having = predicate;

        return *this;
    }

    auto andHaving(const query::AggregatePredicate& predicate) -> ProjectionQuery<Source, Result>&
    {
        data.having = data.having.has_value() ? data.having.value() && predicate : predicate;

        return *this;
    }

    auto orHaving(const query::AggregatePredicate& predicate) -> ProjectionQuery<Source, Result>&
    {
        data.having = data.having.has_value() ? data.having.value() || predicate : predicate;

        return *this;
    }

    inline auto distinct() -> ProjectionQuery<Source, Result>&
    {
        data.isDistinct = true;

        return *this;
    }

    inline auto offset(std::size_t offset) -> ProjectionQuery<Source, Result>&
    {
        data.offset = offset;

        return *this;
    }

    inline auto limit(std::size_t limit) -> ProjectionQuery<Source, Result>&
    {
        data.limit = limit;

        return *this;
    }

    inline auto disableJoining() -> ProjectionQuery<Source, Result>&
    {
        data.shouldJoin = false;

        return *this;
    }

private:
    friend class orm::Database;

    [[nodiscard]] inline auto getData() const -> const query::QueryData&
    {
        detail::validateProjectionAliases<Result>(data.projections);

        return data;
    }

    query::QueryData data;
};
} // namespace orm
