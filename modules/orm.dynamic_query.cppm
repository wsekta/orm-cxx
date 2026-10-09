module;

#include <algorithm>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

export module orm:dynamic_query;

import orm.reflection;
import :foundation;
import :model;
import :expressions;

// query.hpp
namespace orm
{
export
{

    /**
     * @brief A template class representing a select query in the ORM framework.
     *
     * This class provides functionality for building a select query.
     *
     * @tparam T The type of the query.
     */
    template <typename T>
    class Query
    {
    public:
        /**
         * @brief Constructs a Query object.
         */
        Query() = default;

        /**
         * @brief Replaces the WHERE predicate.
         * @param predicate
         */
        template <typename P>
            requires query::detail::PredicateFor<P, T>
        auto where(const P& predicate) -> Query<T>&
        {
            data.predicate = query::detail::erase(predicate);

            return *this;
        }

        /**
         * @brief Adds a predicate with AND.
         * @param predicate
         */
        template <typename P>
            requires query::detail::PredicateFor<P, T>
        auto andWhere(const P& predicate) -> Query<T>&
        {
            data.predicate = data.predicate.has_value() ? data.predicate.value() && query::detail::erase(predicate) :
                                                          query::detail::erase(predicate);

            return *this;
        }

        /**
         * @brief Adds a predicate with OR.
         * @param predicate
         */
        template <typename P>
            requires query::detail::PredicateFor<P, T>
        auto orWhere(const P& predicate) -> Query<T>&
        {
            data.predicate = data.predicate.has_value() ? data.predicate.value() || query::detail::erase(predicate) :
                                                          query::detail::erase(predicate);

            return *this;
        }

        /**
         * @brief Sets the ORDER BY clauses for the select query.
         * @param orders The ORDER BY clauses.
         *
         * @return A reference to the QueryBuilder object.
         */
        template <typename... Orders>
            requires query::detail::ORM_QUERY_MODEL_ORDERS<T, Orders...>
        auto orderBy(Orders... orders) -> Query<T>&
        {
            data.orderBy = {query::detail::erase(orders)...};

            return *this;
        }

        /**
         * @brief Sets the GROUP BY columns for the select query.
         * @param columns The model column paths to
         * group by.
         * @return A reference to this query.
         */
        template <typename... Columns>
            requires query::detail::ORM_QUERY_MODEL_COLUMNS<T, Columns...>
        auto groupBy(Columns... columns) -> Query<T>&
        {
            data.groupBy = {query::detail::erase(columns)...};

            return *this;
        }

        /**
         * @brief Replaces the HAVING predicate.
         * @param predicate The aggregate predicate.
         * @return A
         * reference to this query.
         */
        template <typename P>
            requires query::detail::AggregatePredicateFor<P, T>
        auto having(const P& predicate) -> Query<T>&
        {
            data.having = query::detail::erase(predicate);

            return *this;
        }

        /**
         * @brief Adds an aggregate predicate with AND.
         * @param predicate The aggregate predicate.
         *
         * @return A reference to this query.
         */
        template <typename P>
            requires query::detail::AggregatePredicateFor<P, T>
        auto andHaving(const P& predicate) -> Query<T>&
        {
            data.having = data.having.has_value() ? data.having.value() && query::detail::erase(predicate) :
                                                    query::detail::erase(predicate);

            return *this;
        }

        /**
         * @brief Adds an aggregate predicate with OR.
         * @param predicate The aggregate predicate.
         *
         * @return A reference to this query.
         */
        template <typename P>
            requires query::detail::AggregatePredicateFor<P, T>
        auto orHaving(const P& predicate) -> Query<T>&
        {
            data.having = data.having.has_value() ? data.having.value() || query::detail::erase(predicate) :
                                                    query::detail::erase(predicate);

            return *this;
        }

        /**
         * @brief Select distinct rows.
         * @return A reference to the QueryBuilder object.
         */
        inline auto distinct() -> Query<T>&
        {
            data.isDistinct = true;

            return *this;
        }

        /**
         * @brief Sets the OFFSET clause for the select query.
         * @param offset The number of rows to skip.
         * @return A reference to the QueryBuilder object.
         */
        inline auto offset(std::size_t offset) -> Query<T>&
        {
            data.offset = offset;

            return *this;
        }

        /**
         * @brief Sets the LIMIT clause for the select query.
         * @param limit The maximum number of rows to return.
         * @return A reference to the QueryBuilder object.
         */
        inline auto limit(std::size_t limit) -> Query<T>&
        {
            data.limit = limit;

            return *this;
        }

        /**
         * @brief Disables joining for the select query.
         * @note Only ids fields will be set in related models.
         * @return A reference to the QueryBuilder object.
         */
        inline auto disableJoining() -> Query<T>&
        {
            data.shouldJoin = false;

            return *this;
        }

        /**
         * @brief Explicitly loads a mapped OneToMany or ManyToMany collection.
         *
         * Repeating the same
         * include is idempotent. Collection loading is performed
         * after the root SELECT, so root pagination is
         * preserved.
         */
        template <auto Member>
            requires query::detail::ORM_QUERY_COLLECTION<Member> &&
                         query::detail::ORM_QUERY_MODEL_TYPE<typename query::detail::CollectionTraits<Member>::Model, T>
        auto include() -> Query<T>&
        {
            const auto relation = query::detail::CollectionTraits<Member>::name();
            if (std::ranges::find(data.includes, relation) == data.includes.end())
                data.includes.push_back(relation);
            return *this;
        }

    private:
        /**
         * @brief Database class is a friend class of Query for access to the query data.
         */
        template <typename>
        friend class orm::Database;
        friend class orm::DatabaseCore;

        /**
         * @brief Gets the query data.
         * @return The query data.
         */
        [[nodiscard]] inline auto getData() const -> const query::detail::SelectSpec&
        {
            return data;
        }

        query::detail::SelectSpec data; /**< Runtime query options; model metadata comes from Database<Schema>. */
    };
}
} // namespace orm

// projection_query.hpp
namespace orm
{

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

template <typename Projection>
auto projectionAlias(const Projection& projection) -> std::string_view
{
    if constexpr (requires { projection.resultField; })
        return projection.resultField;
    else
        return projection.alias;
}

template <typename Projections>
inline auto validateProjectionAliasNames(const Projections& projections,
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
        const std::string alias{projectionAlias(projection)};
        if (alias.empty())
        {
            throw std::invalid_argument{"Projection alias must not be empty"};
        }

        if (not resultFields.contains(alias))
        {
            throw std::invalid_argument{"Projection alias does not match a result field: " + alias};
        }

        if (not projectedFields.insert(alias).second)
        {
            throw std::invalid_argument{"Duplicate projection alias: " + alias};
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

template <typename Result, typename Projections>
auto validateProjectionAliases(const Projections& projections) -> void
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
}
export
{
    // namespace detail

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
            requires query::detail::ORM_QUERY_MODEL_PROJECTIONS<Source, Projections...>
        auto project(Projections... projections) -> ProjectionQuery<Source, Result>&
        {
            data.projections = {query::detail::erase(projections)...};
            detail::validateProjectionAliases<Result>(data.projections);

            return *this;
        }

        template <typename P>
            requires query::detail::PredicateFor<P, Source>
        auto where(const P& predicate) -> ProjectionQuery<Source, Result>&
        {
            data.predicate = query::detail::erase(predicate);

            return *this;
        }

        template <typename P>
            requires query::detail::PredicateFor<P, Source>
        auto andWhere(const P& predicate) -> ProjectionQuery<Source, Result>&
        {
            data.predicate = data.predicate.has_value() ? data.predicate.value() && query::detail::erase(predicate) :
                                                          query::detail::erase(predicate);

            return *this;
        }

        template <typename P>
            requires query::detail::PredicateFor<P, Source>
        auto orWhere(const P& predicate) -> ProjectionQuery<Source, Result>&
        {
            data.predicate = data.predicate.has_value() ? data.predicate.value() || query::detail::erase(predicate) :
                                                          query::detail::erase(predicate);

            return *this;
        }

        template <typename... Orders>
            requires query::detail::ORM_QUERY_MODEL_ORDERS<Source, Orders...>
        auto orderBy(Orders... orders) -> ProjectionQuery<Source, Result>&
        {
            data.orderBy = {query::detail::erase(orders)...};

            return *this;
        }

        template <typename... Columns>
            requires query::detail::ORM_QUERY_MODEL_COLUMNS<Source, Columns...>
        auto groupBy(Columns... columns) -> ProjectionQuery<Source, Result>&
        {
            data.groupBy = {query::detail::erase(columns)...};

            return *this;
        }

        template <typename P>
            requires query::detail::AggregatePredicateFor<P, Source>
        auto having(const P& predicate) -> ProjectionQuery<Source, Result>&
        {
            data.having = query::detail::erase(predicate);

            return *this;
        }

        template <typename P>
            requires query::detail::AggregatePredicateFor<P, Source>
        auto andHaving(const P& predicate) -> ProjectionQuery<Source, Result>&
        {
            data.having = data.having.has_value() ? data.having.value() && query::detail::erase(predicate) :
                                                    query::detail::erase(predicate);

            return *this;
        }

        template <typename P>
            requires query::detail::AggregatePredicateFor<P, Source>
        auto orHaving(const P& predicate) -> ProjectionQuery<Source, Result>&
        {
            data.having = data.having.has_value() ? data.having.value() || query::detail::erase(predicate) :
                                                    query::detail::erase(predicate);

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

        [[nodiscard]] inline auto getData() const -> const query::detail::SelectSpec&
        {
            detail::validateProjectionAliases<Result>(data.projections);

            return data;
        }

        query::detail::SelectSpec data;
    };
}
} // namespace orm

// update.hpp
namespace orm
{
export
{

    template <typename T>
    class Update
    {
    public:
        Update() = default;

        template <auto... Members, typename Value>
            requires query::detail::ColumnFor<query::TypedColumn<Members...>, T> &&
                         query::detail::ORM_QUERY_WRITE_SAFE<query::TypedColumn<Members...>> &&
                         query::detail::ORM_QUERY_VALUE<typename query::TypedColumn<Members...>::Value, Value>
        auto set(query::TypedColumn<Members...> column, Value&& value) -> Update<T>&
        {
            data.assignments.push_back(query::detail::UpdateAssignment{
                .column = query::detail::erase(column),
                .value = query::detail::UpdateValue{
                    query::detail::typedValue<typename query::TypedColumn<Members...>::Value>(
                        std::forward<Value>(value))}});
            return *this;
        }

        template <auto... Members, typename Value>
            requires query::detail::ColumnFor<query::TypedColumn<Members...>, T> &&
                         query::detail::ORM_QUERY_WRITE_SAFE<query::TypedColumn<Members...>> &&
                         query::detail::ORM_QUERY_NULLABLE<query::TypedColumn<Members...>::nullable> &&
                         query::detail::ORM_QUERY_VALUE<typename query::TypedColumn<Members...>::Value, Value>
        auto set(query::TypedColumn<Members...> column, const std::optional<Value>& value) -> Update<T>&
        {
            if (value.has_value())
                set(column, value.value());
            else
                set(column, std::nullopt);
            return *this;
        }

        template <auto... Members>
            requires query::detail::ColumnFor<query::TypedColumn<Members...>, T> &&
                         query::detail::ORM_QUERY_WRITE_SAFE<query::TypedColumn<Members...>> &&
                         query::detail::ORM_QUERY_NULLABLE<query::TypedColumn<Members...>::nullable>
        auto set(query::TypedColumn<Members...> column, std::nullopt_t) -> Update<T>&
        {
            data.assignments.push_back(query::detail::UpdateAssignment{query::detail::erase(column), {}});
            return *this;
        }

        template <typename P>
            requires query::detail::PredicateFor<P, T> && query::detail::ORM_QUERY_WRITE_SAFE<P>
        auto where(const P& predicate) -> Update<T>&
        {
            data.predicate = query::detail::erase(predicate);
            return *this;
        }
        template <typename P>
            requires query::detail::PredicateFor<P, T> && query::detail::ORM_QUERY_WRITE_SAFE<P>
        auto andWhere(const P& predicate) -> Update<T>&
        {
            const auto erased = query::detail::erase(predicate);
            data.predicate = data.predicate.has_value() ? data.predicate.value() && erased : erased;
            return *this;
        }
        template <typename P>
            requires query::detail::PredicateFor<P, T> && query::detail::ORM_QUERY_WRITE_SAFE<P>
        auto orWhere(const P& predicate) -> Update<T>&
        {
            const auto erased = query::detail::erase(predicate);
            data.predicate = data.predicate.has_value() ? data.predicate.value() || erased : erased;
            return *this;
        }

    private:
        template <typename>
        friend class orm::Database;
        friend class orm::DatabaseCore;

        [[nodiscard]] auto getData() const -> const query::detail::UpdateSpec&
        {
            return data;
        }

        query::detail::UpdateSpec data;
    };
}
} // namespace orm
