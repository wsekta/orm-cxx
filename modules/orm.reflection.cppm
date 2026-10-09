module;

#include <array>
#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <source_location>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

export module orm.reflection;

export import :generated;

export namespace orm::reflection
{
/**
 * A structural, null-terminated compile-time string.
 *
 * N is the number of characters, excluding the terminating null character.
 */
template <std::size_t N>
struct FixedString
{
    char value[N + 1]{};

    constexpr FixedString() noexcept = default;

    constexpr FixedString(const char (&text)[N + 1]) noexcept
    {
        for (std::size_t index = 0; index < N; ++index)
        {
            value[index] = text[index];
        }
        value[N] = '\0';
    }

    [[nodiscard]] constexpr auto size() const noexcept -> std::size_t
    {
        return N;
    }

    [[nodiscard]] constexpr auto empty() const noexcept -> bool
    {
        return N == 0;
    }

    [[nodiscard]] constexpr auto data() const& noexcept -> const char*
    {
        return value;
    }
    auto data() const&& -> const char* = delete;

    [[nodiscard]] constexpr auto c_str() const& noexcept -> const char*
    {
        return value;
    }
    auto c_str() const&& -> const char* = delete;

    [[nodiscard]] constexpr auto view() const& noexcept -> std::string_view
    {
        return {value, N}; // NOLINT(bugprone-string-constructor)
    }
    auto view() const&& -> std::string_view = delete;

    constexpr operator std::string_view() const& noexcept
    {
        return view();
    }
    operator std::string_view() const&& = delete;

    [[nodiscard]] constexpr auto operator[](std::size_t index) const noexcept -> char
    {
        return value[index];
    }

    constexpr auto operator==(const FixedString&) const noexcept -> bool = default;
};

template <std::size_t N>
FixedString(const char (&)[N]) -> FixedString<N - 1>;

template <std::size_t LeftSize, std::size_t RightSize>
[[nodiscard]] constexpr auto operator==(const FixedString<LeftSize>& left,
                                        const FixedString<RightSize>& right) noexcept -> bool
    requires(LeftSize != RightSize)
{
    return left.view() == right.view();
}

template <std::size_t N>
[[nodiscard]] constexpr auto operator==(const FixedString<N>& left, std::string_view right) noexcept -> bool
{
    return left.view() == right;
}

template <std::size_t N>
[[nodiscard]] constexpr auto operator==(std::string_view left, const FixedString<N>& right) noexcept -> bool
{
    return left == right.view();
}
} // namespace orm::reflection

#if defined(_MSC_VER)
namespace orm::reflection::detail
{
[[nodiscard]] consteval auto isMsvcIdentifierCharacter(char character) noexcept -> bool
{
    const auto byte = static_cast<unsigned char>(character);
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9') || character == '_' || byte >= 0x80U;
}

[[nodiscard]] consteval auto parseTypeSignature(std::string_view signature) noexcept -> std::string_view
{
    constexpr std::string_view marker = "typeNameView<";
    const auto markerPosition = signature.find(marker);
    const auto end = signature.rfind(">(void)");
    if (markerPosition == std::string_view::npos || end == std::string_view::npos)
    {
        return {};
    }
    const auto begin = markerPosition + marker.size();
    return begin <= end ? signature.substr(begin, end - begin) : std::string_view{};
}

[[nodiscard]] consteval auto parseValueSignature(std::string_view signature) noexcept -> std::string_view
{
    constexpr std::string_view marker = "valueNameView<";
    const auto markerPosition = signature.find(marker);
    const auto end = signature.rfind(">(void)");
    if (markerPosition == std::string_view::npos || end == std::string_view::npos)
    {
        return {};
    }
    const auto begin = markerPosition + marker.size();
    return begin <= end ? signature.substr(begin, end - begin) : std::string_view{};
}

[[nodiscard]] consteval auto parsePointedFieldSignature(std::string_view signature) noexcept -> std::string_view
{
    auto end = signature.rfind('}');
    if (end == std::string_view::npos)
    {
        return {};
    }
    while (end > 0 && !isMsvcIdentifierCharacter(signature[end - 1]))
    {
        --end;
    }
    auto begin = end;
    while (begin > 0 && isMsvcIdentifierCharacter(signature[begin - 1]))
    {
        --begin;
    }
    return signature.substr(begin, end - begin);
}
} // namespace orm::reflection::detail

#elif defined(__clang__)
namespace orm::reflection::detail
{
[[nodiscard]] consteval auto isClangIdentifierCharacter(char character) noexcept -> bool
{
    const auto byte = static_cast<unsigned char>(character);
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9') || character == '_' || byte >= 0x80U;
}

[[nodiscard]] consteval auto parseTypeSignature(std::string_view signature) noexcept -> std::string_view
{
    constexpr std::string_view marker = "T = ";
    const auto markerPosition = signature.find(marker);
    const auto end = signature.rfind(']');
    if (markerPosition == std::string_view::npos || end == std::string_view::npos)
    {
        return {};
    }
    const auto begin = markerPosition + marker.size();
    return begin <= end ? signature.substr(begin, end - begin) : std::string_view{};
}

[[nodiscard]] consteval auto parseValueSignature(std::string_view signature) noexcept -> std::string_view
{
    constexpr std::string_view marker = "Value = ";
    const auto markerPosition = signature.find(marker);
    const auto end = signature.rfind(']');
    if (markerPosition == std::string_view::npos || end == std::string_view::npos)
    {
        return {};
    }
    const auto begin = markerPosition + marker.size();
    return begin <= end ? signature.substr(begin, end - begin) : std::string_view{};
}

[[nodiscard]] consteval auto parsePointedFieldSignature(std::string_view signature) noexcept -> std::string_view
{
    auto end = signature.rfind('}');
    if (end == std::string_view::npos)
    {
        return {};
    }
    while (end > 0 && !isClangIdentifierCharacter(signature[end - 1]))
    {
        --end;
    }
    auto begin = end;
    while (begin > 0 && isClangIdentifierCharacter(signature[begin - 1]))
    {
        --begin;
    }
    return signature.substr(begin, end - begin);
}
} // namespace orm::reflection::detail

#elif defined(__GNUC__)
namespace orm::reflection::detail
{
[[nodiscard]] consteval auto isGccIdentifierCharacter(char character) noexcept -> bool
{
    const auto byte = static_cast<unsigned char>(character);
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9') || character == '_' || byte >= 0x80U;
}

[[nodiscard]] consteval auto parseGccTemplateArgument(std::string_view signature,
                                                      std::string_view marker) noexcept -> std::string_view
{
    const auto markerPosition = signature.find(marker);
    if (markerPosition == std::string_view::npos)
    {
        return {};
    }
    const auto begin = markerPosition + marker.size();
    const auto separator = signature.find(';', begin);
    const auto closingBracket = signature.rfind(']');
    const auto end = separator == std::string_view::npos ? closingBracket : separator;
    if (end == std::string_view::npos || begin > end)
    {
        return {};
    }
    return signature.substr(begin, end - begin);
}

[[nodiscard]] consteval auto parseTypeSignature(std::string_view signature) noexcept -> std::string_view
{
    return parseGccTemplateArgument(signature, "T = ");
}

[[nodiscard]] consteval auto parseValueSignature(std::string_view signature) noexcept -> std::string_view
{
    return parseGccTemplateArgument(signature, "Value = ");
}

[[nodiscard]] consteval auto parsePointedFieldSignature(std::string_view signature) noexcept -> std::string_view
{
    auto end = signature.rfind('}');
    if (end == std::string_view::npos)
    {
        return {};
    }
    while (end > 0 && !isGccIdentifierCharacter(signature[end - 1]))
    {
        --end;
    }
    auto begin = end;
    while (begin > 0 && isGccIdentifierCharacter(signature[begin - 1]))
    {
        --begin;
    }
    return signature.substr(begin, end - begin);
}
} // namespace orm::reflection::detail

#else
#error "orm::reflection supports Clang, GCC, and MSVC."
#endif

namespace orm::reflection
{
namespace detail
{
template <std::size_t N>
[[nodiscard]] consteval auto makeFixedString(std::string_view text) noexcept -> FixedString<N>
{
    FixedString<N> result;
    for (std::size_t index = 0; index < N; ++index)
    {
        result.value[index] = text[index];
    }
    result.value[N] = '\0';
    return result;
}

template <typename T>
[[nodiscard]] consteval auto typeNameView() noexcept
{
    // Deliberately not constexpr: GCC otherwise captures the primary template
    // signature before T is substituted.
    const auto signature = std::string_view{std::source_location::current().function_name()};
    return parseTypeSignature(signature);
}

[[nodiscard]] consteval auto isTypeIdentifierCharacter(char character) noexcept -> bool
{
    const auto byte = static_cast<unsigned char>(character);
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9') || character == '_' || byte >= 0x80U;
}

[[nodiscard]] consteval auto elaboratedTypePrefixSize(std::string_view spelling,
                                                      std::size_t position) noexcept -> std::size_t
{
    if (position != 0 && isTypeIdentifierCharacter(spelling[position - 1]))
    {
        return 0;
    }
    constexpr std::array prefixes{std::string_view{"class "}, std::string_view{"struct "}, std::string_view{"enum "},
                                  std::string_view{"union "}};
    for (const auto prefix : prefixes)
    {
        if (spelling.substr(position).starts_with(prefix))
        {
            return prefix.size();
        }
    }
    return 0;
}

[[nodiscard]] consteval auto normalizedTypeNameSize(std::string_view spelling) noexcept -> std::size_t
{
    std::size_t result{};
    for (std::size_t index = 0; index < spelling.size();)
    {
        const auto prefixSize = elaboratedTypePrefixSize(spelling, index);
        if (prefixSize != 0)
        {
            index += prefixSize;
        }
        else
        {
            ++result;
            ++index;
        }
    }
    return result;
}

template <std::size_t Size>
[[nodiscard]] consteval auto normalizeTypeName(std::string_view spelling) noexcept -> FixedString<Size>
{
    FixedString<Size> result;
    std::size_t output{};
    for (std::size_t index = 0; index < spelling.size();)
    {
        const auto prefixSize = elaboratedTypePrefixSize(spelling, index);
        if (prefixSize != 0)
        {
            index += prefixSize;
        }
        else
        {
            result.value[output++] = spelling[index++];
        }
    }
    result.value[Size] = '\0';
    return result;
}
} // namespace detail

export
{
    template <typename T>
    [[nodiscard]] consteval auto typeName() noexcept
    {
        constexpr auto spelling = detail::typeNameView<T>();
        constexpr auto size = detail::normalizedTypeNameSize(spelling);
        return detail::normalizeTypeName<size>(spelling);
    }

    template <typename T>
    inline constexpr auto typeNameStorage = typeName<T>();

    template <typename T>
    [[nodiscard]] consteval auto getTypeName() noexcept -> std::string_view
    {
        return typeNameStorage<T>.view();
    }
}

namespace detail
{
struct TypeNameSignatureSentinel
{
};
static_assert(getTypeName<TypeNameSignatureSentinel>().ends_with("TypeNameSignatureSentinel"),
              "ORM_REFLECTION_TYPE_SIGNATURE_FORMAT: unsupported compiler type-signature format");
} // namespace detail
} // namespace orm::reflection

namespace orm::reflection
{
namespace detail
{
template <auto Value>
[[nodiscard]] consteval auto valueNameView() noexcept
{
    const auto signature = std::string_view{std::source_location::current().function_name()};
    return parseValueSignature(signature);
}

template <typename T>
struct PointerWrapper
{
    T value;
};

template <typename T>
PointerWrapper(T) -> PointerWrapper<T>;

template <typename T>
[[nodiscard]] constexpr auto wrapPointer(const T& pointer) noexcept -> PointerWrapper<T>
{
    return {pointer};
}

template <auto WrappedPointer>
[[nodiscard]] consteval auto pointedFieldNameView() noexcept
{
    const auto signature = std::string_view{std::source_location::current().function_name()};
    return parsePointedFieldSignature(signature);
}

template <typename Class, typename Member>
auto memberClass(Member Class::*) -> Class;

template <typename T>
union InactiveStorage
{
    char inactive;
    T object;

    constexpr InactiveStorage() noexcept : inactive{} {}
    constexpr ~InactiveStorage() {}
};

template <typename T>
inline constexpr T memberNameObjectStorage{};

template <auto Member>
[[nodiscard]] constexpr auto storedMemberPointer() noexcept
{
    using Class = decltype(memberClass(Member));
    return std::addressof(memberNameObjectStorage<InactiveStorage<InactiveStorage<Class>>>.object.object.*Member);
}

[[nodiscard]] consteval auto isIdentifierStart(char character) noexcept -> bool
{
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || character == '_' ||
           static_cast<unsigned char>(character) >= 0x80U;
}

[[nodiscard]] consteval auto isIdentifierContinuation(char character) noexcept -> bool
{
    return isIdentifierStart(character) || (character >= '0' && character <= '9');
}

[[nodiscard]] consteval auto unqualifiedMemberName(std::string_view spelling) noexcept -> std::string_view
{
    const auto scope = spelling.rfind("::");
    auto begin = scope == std::string_view::npos ? 0 : scope + 2;
    while (begin < spelling.size() && !isIdentifierStart(spelling[begin]))
    {
        ++begin;
    }

    auto end = begin;
    while (end < spelling.size() && isIdentifierContinuation(spelling[end]))
    {
        ++end;
    }
    return spelling.substr(begin, end - begin);
}
} // namespace detail

export
{
    template <auto Value>
    [[nodiscard]] consteval auto valueName() noexcept
    {
        constexpr auto name = detail::valueNameView<Value>();
        return detail::makeFixedString<name.size()>(name);
    }

    template <auto Member>
    [[nodiscard]] consteval auto memberName() noexcept
    {
#if defined(_MSC_VER)
        if constexpr (std::is_member_object_pointer_v<decltype(Member)>)
        {
            constexpr auto pointer = detail::wrapPointer(detail::storedMemberPointer<Member>());
            constexpr auto name = detail::pointedFieldNameView<pointer>();
            return detail::makeFixedString<name.size()>(name);
        }
        else
#endif
        {
            constexpr auto spelling = detail::valueNameView<Member>();
            constexpr auto name = detail::unqualifiedMemberName(spelling);
            return detail::makeFixedString<name.size()>(name);
        }
    }
}

namespace detail
{
struct NameSignatureSentinel
{
    int member;
};

enum class ValueSignatureSentinel
{
    enumerator
};

inline constexpr auto valueSignatureSentinelName = valueName<ValueSignatureSentinel::enumerator>();

static_assert(memberName<&NameSignatureSentinel::member>() == "member",
              "ORM_REFLECTION_MEMBER_SIGNATURE_FORMAT: unsupported compiler member-signature format");
static_assert(valueSignatureSentinelName.view().ends_with("enumerator"),
              "ORM_REFLECTION_VALUE_SIGNATURE_FORMAT: unsupported compiler value-signature format");
} // namespace detail
} // namespace orm::reflection

namespace orm::reflection
{
export
{
    inline constexpr std::size_t maxFieldCount = 128;

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

export
{
    template <typename T, std::size_t Index>
    using field_type_t = std::remove_reference_t<
        std::tuple_element_t<Index, typename detail::ReflectionTraits<detail::ReflectedType<T>>::Tuple>>;
}

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
