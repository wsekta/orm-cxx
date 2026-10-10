module;

#include <algorithm>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

export module orm:dynamic_update;

import orm.reflection;
import :foundation;
import :model;
import :expressions;
import :dynamic_select;

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
        friend class orm::OrmContext;
        friend class orm::detail::QueryTestAccess;

        [[nodiscard]] auto getData() const -> const query::detail::UpdateSpec&
        {
            return data;
        }

        query::detail::UpdateSpec data;
    };
}
} // namespace orm
