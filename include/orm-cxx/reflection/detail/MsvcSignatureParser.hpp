#pragma once

#include <string_view>

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
