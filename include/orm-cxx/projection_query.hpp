#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#include "orm-cxx/model/ColumnType.hpp"
#include "orm-cxx/model/Mapping.hpp"
#include "orm-cxx/query/SelectSpec.hpp"
#include "orm-cxx/reflection/Reflection.hpp"

namespace orm
{
class DatabaseCore;
template <typename SchemaType>
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
        return false;
    }

    return false;
}

struct ProjectionResultField
{
    std::string name;
    std::optional<model::ColumnType> type;
};

template <typename T>
consteval auto projectionLogicalType() -> std::optional<model::ColumnType>
{
    using field_t = std::remove_cvref_t<T>;
    using value_t = std::remove_cv_t<model::detail::static_optional_value_t<field_t>>;
    if constexpr (requires { model::LogicalTypeTraits<value_t>::value; })
    {
        return model::LogicalTypeTraits<value_t>::value;
    }
    else
    {
        return std::nullopt;
    }
}

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
        if (not field.type.has_value() or not isSupportedProjectionResultType(field.type.value()))
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
    std::vector<ProjectionResultField> resultFields;
    resultFields.reserve(reflection::fieldCount<Result>);

    [&]<std::size_t... Indices>(std::index_sequence<Indices...>)
    {
        constexpr auto fields = reflection::fields<Result>();
        auto appendField = [&]<std::size_t Index>()
        {
            constexpr auto type = projectionLogicalType<reflection::field_type_t<Result, Index>>();
            resultFields.emplace_back(std::string{fields[Index].name}, type);
        };
        (appendField.template operator()<Indices>(), ...);
    }(std::make_index_sequence<reflection::fieldCount<Result>>{});

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
    ProjectionQuery() = default;

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
    template <typename>
    friend class orm::Database;
    friend class orm::DatabaseCore;

    [[nodiscard]] inline auto getData() const -> const query::SelectSpec&
    {
        detail::validateProjectionAliases<Result>(data.projections);

        return data;
    }

    query::SelectSpec data;
};
} // namespace orm
