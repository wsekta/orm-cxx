#pragma once

#include <memory>
#include <source_location>
#include <string_view>
#include <type_traits>

#include "orm-cxx/reflection/detail/SignatureParser.hpp"
#include "orm-cxx/reflection/TypeName.hpp"

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
