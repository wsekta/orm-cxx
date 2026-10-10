module;

#include <cstddef>
#include <source_location>
#include <string_view>

export module orm.reflection:compiler_signatures;

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

namespace orm::reflection::detail
{
template <typename T>
[[nodiscard]] consteval auto typeNameView() noexcept
{
    // Deliberately not constexpr: GCC otherwise captures the primary template
    // signature before T is substituted.
    const auto signature = std::string_view{std::source_location::current().function_name()};
    return parseTypeSignature(signature);
}

template <auto Value>
[[nodiscard]] consteval auto valueNameView() noexcept
{
    const auto signature = std::string_view{std::source_location::current().function_name()};
    return parseValueSignature(signature);
}

template <auto WrappedPointer>
[[nodiscard]] consteval auto pointedFieldNameView() noexcept
{
    const auto signature = std::string_view{std::source_location::current().function_name()};
    return parsePointedFieldSignature(signature);
}

} // namespace orm::reflection::detail
