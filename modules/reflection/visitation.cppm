module;

#include <cstddef>
#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>

export module orm.reflection:visitation;

import :binding_traits;
import :aggregate;
import :field_metadata;

namespace orm::reflection
{
namespace detail
{
template <typename T, typename Function, std::size_t Index>
constexpr void invokeForField(T& object, Function& function)
{
    auto tied = tieFields(object);
    auto&& field = std::get<Index>(tied);
    constexpr auto descriptor = fields<ReflectedType<T>>()[Index];
    using CompileTimeIndex = std::integral_constant<std::size_t, Index>;

    if constexpr (std::is_invocable_v<Function&, CompileTimeIndex, FieldDescriptor, decltype(field)>)
    {
        std::invoke(function, CompileTimeIndex{}, descriptor, field);
    }
    else if constexpr (std::is_invocable_v<Function&, FieldDescriptor, decltype(field)>)
    {
        std::invoke(function, descriptor, field);
    }
    else if constexpr (std::is_invocable_v<Function&, decltype(field)>)
    {
        std::invoke(function, field);
    }
    else
    {
        static_assert(alwaysFalse<Function>,
                      "forEachField callback must accept (index_constant, FieldDescriptor, field), "
                      "(FieldDescriptor, field), or (field).");
    }
}

template <typename T, typename Function, std::size_t... Indices>
constexpr void forEachFieldImpl(T& object, Function& function, std::index_sequence<Indices...>)
{
    (invokeForField<T, Function, Indices>(object, function), ...);
}
} // namespace detail

export
{
    template <typename T, typename Function>
    constexpr void forEachField(T & object, Function && function)
        requires(!std::is_const_v<T>)
    {
        detail::forEachFieldImpl(object, function, std::make_index_sequence<fieldCount<T>>{});
    }

    template <typename T, typename Function>
    constexpr void forEachField(const T& object, Function&& function)
    {
        detail::forEachFieldImpl(object, function, std::make_index_sequence<fieldCount<T>>{});
    }

    template <typename T, typename Function>
    auto forEachField(T&&, Function&&)
        requires(!std::is_lvalue_reference_v<T>)
    = delete;
}
} // namespace orm::reflection
