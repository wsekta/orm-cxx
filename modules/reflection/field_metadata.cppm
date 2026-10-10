module;

#include <array>
#include <cstddef>
#include <string_view>
#include <tuple>
#include <utility>

export module orm.reflection:field_metadata;

import :fixed_string;
import :compiler_signatures;
import :storage;
import :type_names;
import :value_names;
import :aggregate;

namespace orm::reflection
{
export
{
    struct FieldDescriptor
    {
        std::size_t index;
        std::string_view name;
        std::string_view typeName;

        constexpr auto operator==(const FieldDescriptor&) const noexcept -> bool = default;
    };
}

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

export
{
    template <typename T, std::size_t Index>
    [[nodiscard]] consteval auto fieldName() noexcept
    {
        using Type = detail::ReflectedType<T>;
        return detail::fieldNameStorage<Type, Index>;
    }
}

namespace detail
{
static_assert(fieldName<NameSignatureSentinel, 0>() == "member",
              "ORM_REFLECTION_FIELD_SIGNATURE_FORMAT: unsupported compiler field-signature format");
} // namespace detail

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

export
{
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
}
} // namespace orm::reflection
