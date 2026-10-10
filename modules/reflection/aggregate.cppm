module;

#include <cstddef>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

export module orm.reflection:aggregate;

import :generated;
import :storage;

namespace orm::reflection
{
export
{
    inline constexpr std::size_t maxFieldCount = 128;
}

namespace detail
{
struct Any
{
    template <typename T>
    operator T() const noexcept;
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
[[nodiscard]] consteval auto fakeObject() noexcept -> const T&
{
    return fakeObjectStorage<T>.object.object;
}
} // namespace detail

export
{
    template <typename T>
    inline constexpr std::size_t fieldCount = detail::ReflectionTraits<detail::ReflectedType<T>>::count;

    template <typename T>
    [[nodiscard]] constexpr auto tieFields(T & value) noexcept
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
    [[nodiscard]] constexpr auto fieldPointers(T & value) noexcept
        requires(!std::is_const_v<T>)
    {
        return std::apply([]<typename... Fields>(Fields&... fields) { return std::tuple{std::addressof(fields)...}; },
                          tieFields(value));
    }

    template <typename T>
    [[nodiscard]] constexpr auto fieldPointers(const T& value) noexcept
    {
        return std::apply([]<typename... Fields>(const Fields&... fields)
                          { return std::tuple{std::addressof(fields)...}; }, tieFields(value));
    }

    template <typename T>
    auto fieldPointers(T&&)
        requires(!std::is_lvalue_reference_v<T>)
    = delete;
}

export
{
    template <typename T, std::size_t Index>
    using field_type_t = std::remove_reference_t<
        std::tuple_element_t<Index, typename detail::ReflectionTraits<detail::ReflectedType<T>>::Tuple>>;
}
} // namespace orm::reflection
