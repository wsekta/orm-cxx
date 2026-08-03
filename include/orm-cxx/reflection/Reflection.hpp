#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include "orm-cxx/reflection/detail/GeneratedBindings.hpp"
#include "orm-cxx/reflection/MemberName.hpp"
#include "orm-cxx/reflection/TypeName.hpp"

namespace orm::reflection
{
inline constexpr std::size_t maxFieldCount = 128;

struct FieldDescriptor
{
    std::size_t index;
    std::string_view name;
    std::string_view typeName;

    constexpr auto operator==(const FieldDescriptor&) const noexcept -> bool = default;
};

namespace detail
{
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wundefined-inline"
#endif

struct Any
{
    template <typename T>
    constexpr operator T() const noexcept;
};

template <typename T, std::size_t... Indices>
[[nodiscard]] consteval auto isAggregateInitializable(std::index_sequence<Indices...>) noexcept -> bool
{
    return requires { T{(static_cast<void>(Indices), Any{})...}; };
}

template <typename T, std::size_t N>
inline constexpr bool isAggregateInitializableWith = isAggregateInitializable<T>(std::make_index_sequence<N>{});

template <typename T, std::size_t N = 0>
[[nodiscard]] consteval auto aggregateFieldCount() noexcept -> std::size_t
{
    if constexpr (N == maxFieldCount)
    {
        static_assert(!isAggregateInitializableWith<T, maxFieldCount + 1>,
                      "orm::reflection supports aggregates with at most 128 fields.");
        return maxFieldCount;
    }
    else if constexpr (isAggregateInitializableWith<T, N> && !isAggregateInitializableWith<T, N + 1>)
    {
        return N;
    }
    else
    {
        return aggregateFieldCount<T, N + 1>();
    }
}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

template <typename T>
inline constexpr bool supportedAggregateCategory = std::is_aggregate_v<T> && !std::is_union_v<T> && !std::is_array_v<T>;

template <typename T, bool = supportedAggregateCategory<T>>
struct ReflectionTraits;

template <typename T>
struct ReflectionTraits<T, false>
{
    static_assert(std::is_aggregate_v<T>, "ORM_REFLECTION_AGGREGATE: orm::reflection requires an aggregate type.");
    static_assert(!std::is_union_v<T>, "orm::reflection does not support unions.");
    static_assert(!std::is_array_v<T>, "orm::reflection does not support raw C arrays.");

    static constexpr std::size_t count = 0;
    using Tuple = std::tuple<>;
};

template <typename T>
struct ReflectionTraits<T, true>
{
    static constexpr std::size_t count = aggregateFieldCount<T>();
    using Binding = decltype(bindingTraitsImpl(std::declval<T&>(), std::integral_constant<std::size_t, count>{}));
    using Tuple = typename Binding::Tuple;

private:
    template <std::size_t... Indices>
    [[nodiscard]] static consteval auto containsRawArray(std::index_sequence<Indices...>) noexcept -> bool
    {
        return (std::is_array_v<std::remove_reference_t<std::tuple_element_t<Indices, Tuple>>> || ... || false);
    }

public:
    static_assert(Binding::fieldsAreAddressable,
                  "ORM_REFLECTION_BIT_FIELD: orm::reflection does not support bit-fields.");
    static_assert(std::tuple_size_v<Tuple> == count,
                  "orm::reflection cannot decompose this aggregate; inheritance is not supported.");
    static_assert(!containsRawArray(std::make_index_sequence<count>{}),
                  "orm::reflection does not support raw C-array fields; use std::array instead.");
};

template <typename T>
using ReflectedType = std::remove_cv_t<std::remove_reference_t<T>>;

template <typename T>
inline constexpr InactiveStorage<InactiveStorage<T>> fakeObjectStorage{};

template <typename T>
[[nodiscard]] constexpr auto fakeObject() noexcept -> const T&
{
    return fakeObjectStorage<T>.object.object;
}
} // namespace detail

template <typename T>
inline constexpr std::size_t fieldCount = detail::ReflectionTraits<detail::ReflectedType<T>>::count;

template <typename T>
[[nodiscard]] constexpr auto tieFields(T& value) noexcept
    requires(!std::is_const_v<T>)
{
    using Type = detail::ReflectedType<T>;
    return detail::tieFieldsImpl(value, std::integral_constant<std::size_t, fieldCount<Type>>{});
}

template <typename T>
[[nodiscard]] constexpr auto tieFields(const T& value) noexcept
{
    using Type = detail::ReflectedType<T>;
    return detail::tieFieldsImpl(value, std::integral_constant<std::size_t, fieldCount<Type>>{});
}

template <typename T>
auto tieFields(T&&)
    requires(!std::is_lvalue_reference_v<T>)
= delete;

template <typename T>
[[nodiscard]] constexpr auto fieldPointers(T& value) noexcept
    requires(!std::is_const_v<T>)
{
    return std::apply([]<typename... Fields>(Fields&... fields) { return std::tuple{std::addressof(fields)...}; },
                      tieFields(value));
}

template <typename T>
[[nodiscard]] constexpr auto fieldPointers(const T& value) noexcept
{
    return std::apply([]<typename... Fields>(const Fields&... fields) { return std::tuple{std::addressof(fields)...}; },
                      tieFields(value));
}

template <typename T>
auto fieldPointers(T&&)
    requires(!std::is_lvalue_reference_v<T>)
= delete;

namespace detail
{
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wundefined-var-template"
#endif

template <typename T, std::size_t Index>
[[nodiscard]] consteval auto makeFieldName() noexcept
{
    static_assert(Index < fieldCount<T>, "Field index is outside the reflected aggregate.");
    constexpr auto pointers = fieldPointers(fakeObject<T>());
    constexpr auto wrapped = wrapPointer(std::get<Index>(pointers));
    constexpr auto name = pointedFieldNameView<wrapped>();
    return makeFixedString<name.size()>(name);
}

template <typename T, std::size_t Index>
inline constexpr auto fieldNameStorage = makeFieldName<T, Index>();

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
} // namespace detail

template <typename T, std::size_t Index>
[[nodiscard]] consteval auto fieldName() noexcept
{
    using Type = detail::ReflectedType<T>;
    return detail::fieldNameStorage<Type, Index>;
}

namespace detail
{
static_assert(fieldName<NameSignatureSentinel, 0>() == "member",
              "ORM_REFLECTION_FIELD_SIGNATURE_FORMAT: unsupported compiler field-signature format");
} // namespace detail

template <typename T, std::size_t Index>
using field_type_t = std::remove_reference_t<
    std::tuple_element_t<Index, typename detail::ReflectionTraits<detail::ReflectedType<T>>::Tuple>>;

namespace detail
{
template <typename T, std::size_t... Indices>
[[nodiscard]] consteval auto makeFieldNames(std::index_sequence<Indices...>) noexcept
{
    return std::array<std::string_view, sizeof...(Indices)>{fieldNameStorage<T, Indices>.view()...};
}

template <typename T>
inline constexpr auto fieldNamesStorage = makeFieldNames<T>(std::make_index_sequence<fieldCount<T>>{});

template <typename T, std::size_t... Indices>
[[nodiscard]] consteval auto makeFieldDescriptors(std::index_sequence<Indices...>) noexcept
{
    return std::array<FieldDescriptor, sizeof...(Indices)>{FieldDescriptor{
        Indices, fieldNameStorage<T, Indices>.view(), typeNameStorage<field_type_t<T, Indices>>.view()}...};
}

template <typename T>
inline constexpr auto fieldDescriptorStorage = makeFieldDescriptors<T>(std::make_index_sequence<fieldCount<T>>{});
} // namespace detail

template <typename T>
[[nodiscard]] consteval auto fieldNames() noexcept
{
    using Type = detail::ReflectedType<T>;
    return detail::fieldNamesStorage<Type>;
}

template <typename T>
[[nodiscard]] consteval auto fields() noexcept
{
    using Type = detail::ReflectedType<T>;
    return detail::fieldDescriptorStorage<Type>;
}

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

template <typename T, typename Function>
constexpr void forEachField(T& object, Function&& function)
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
} // namespace orm::reflection
