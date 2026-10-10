module;

#include <array>
#include <cstddef>
#include <string_view>

export module orm.reflection:type_names;

import :fixed_string;
import :compiler_signatures;

namespace orm::reflection
{
namespace detail
{
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

// The same standard type can include its ABI namespace in one importing unit and omit it in another.
// Normalize that spelling before sizing inline storage shared by those units.
[[nodiscard]] consteval auto standardAbiNamespacePrefixSize(std::string_view spelling,
                                                            std::size_t position) noexcept -> std::size_t
{
    const auto suffix = spelling.substr(position);
    std::size_t prefixSize{};
    if (suffix.starts_with("__cxx11::"))
    {
        prefixSize = std::string_view{"__cxx11::"}.size();
    }
    else if (suffix.starts_with("__ndk1::"))
    {
        prefixSize = std::string_view{"__ndk1::"}.size();
    }
    else if (suffix.starts_with("__"))
    {
        std::size_t end = 2;
        while (end < suffix.size() && suffix[end] >= '0' && suffix[end] <= '9')
        {
            ++end;
        }
        if (end > 2 && suffix.substr(end).starts_with("::"))
        {
            prefixSize = end + 2;
        }
    }
    if (prefixSize == 0)
    {
        return 0;
    }

    auto qualifiedBegin = position;
    while (qualifiedBegin > 0 &&
           (isTypeIdentifierCharacter(spelling[qualifiedBegin - 1]) || spelling[qualifiedBegin - 1] == ':'))
    {
        --qualifiedBegin;
    }
    const auto scope = spelling.substr(qualifiedBegin, position - qualifiedBegin);
    return scope.starts_with("std::") || scope.starts_with("::std::") ? prefixSize : 0;
}

[[nodiscard]] consteval auto ignoredTypePrefixSize(std::string_view spelling,
                                                   std::size_t position) noexcept -> std::size_t
{
    // GCC prints module ownership after a declaration's name. It is metadata,
    // rather than part of the C++ type or its enclosing scopes.
    if (position != 0 && spelling[position] == '@' &&
        (isTypeIdentifierCharacter(spelling[position - 1]) || spelling[position - 1] == '>'))
    {
        const auto identifierBegin = [&](std::size_t index) consteval
        {
            return index < spelling.size() && isTypeIdentifierCharacter(spelling[index]) &&
                   !(spelling[index] >= '0' && spelling[index] <= '9');
        };
        auto end = position + 1;
        bool partition = false;
        while (identifierBegin(end))
        {
            do
            {
                ++end;
            } while (end < spelling.size() && isTypeIdentifierCharacter(spelling[end]));
            if (end == spelling.size())
            {
                return end - position;
            }
            if (spelling[end] == '.' || (spelling[end] == ':' && spelling.substr(end, 2) != "::"))
            {
                if ((spelling[end] == ':' && partition) || !identifierBegin(end + 1))
                {
                    return 0;
                }
                partition = partition || spelling[end] == ':';
                ++end;
            }
            else
            {
                return end - position;
            }
        }
        return 0;
    }
    const auto elaboratedPrefixSize = elaboratedTypePrefixSize(spelling, position);
    return elaboratedPrefixSize != 0 ? elaboratedPrefixSize : standardAbiNamespacePrefixSize(spelling, position);
}

[[nodiscard]] consteval auto typeLiteralSize(std::string_view spelling, std::size_t position) noexcept -> std::size_t
{
    const auto quote = spelling[position];
    // MSVC surrounds anonymous scopes with a backtick and an apostrophe. Copy
    // the entire scope so its closing apostrophe is not read as a char literal.
    if (quote == '`')
    {
        const auto end = spelling.find('\'', position + 1);
        return end == std::string_view::npos ? 0 : end - position + 1;
    }
    if (quote != '\'' && quote != '"')
    {
        return 0;
    }
    auto end = position + 1;
    while (end < spelling.size())
    {
        if (spelling[end] == '\\' && end + 1 < spelling.size())
        {
            end += 2;
        }
        else if (spelling[end++] == quote)
        {
            return end - position;
        }
    }
    return end - position;
}

[[nodiscard]] consteval auto normalizedTypeNameSize(std::string_view spelling) noexcept -> std::size_t
{
    std::size_t result{};
    for (std::size_t index = 0; index < spelling.size();)
    {
        const auto literalSize = typeLiteralSize(spelling, index);
        if (literalSize != 0)
        {
            result += literalSize;
            index += literalSize;
            continue;
        }
        const auto prefixSize = ignoredTypePrefixSize(spelling, index);
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
        const auto literalSize = typeLiteralSize(spelling, index);
        if (literalSize != 0)
        {
            for (std::size_t literalIndex = 0; literalIndex < literalSize; ++literalIndex)
            {
                result.value[output++] = spelling[index++];
            }
            continue;
        }
        const auto prefixSize = ignoredTypePrefixSize(spelling, index);
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
