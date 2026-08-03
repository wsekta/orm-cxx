#pragma once

#include <array>
#include <cstddef>
#include <source_location>
#include <string_view>

#include "orm-cxx/reflection/detail/SignatureParser.hpp"
#include "orm-cxx/reflection/FixedString.hpp"

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

namespace detail
{
struct TypeNameSignatureSentinel
{
};
static_assert(getTypeName<TypeNameSignatureSentinel>().ends_with("TypeNameSignatureSentinel"),
              "ORM_REFLECTION_TYPE_SIGNATURE_FORMAT: unsupported compiler type-signature format");
} // namespace detail
} // namespace orm::reflection
