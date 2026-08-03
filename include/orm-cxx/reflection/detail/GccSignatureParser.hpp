#pragma once

#include <string_view>

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
