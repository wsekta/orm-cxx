#pragma once

#include <string_view>

namespace orm::db::detail
{
[[nodiscard]] constexpr auto isPortableBindName(std::string_view name) noexcept -> bool
{
    if (name.empty())
    {
        return false;
    }

    for (const auto character : name)
    {
        const auto asciiLetter = (character >= 'a' and character <= 'z') or (character >= 'A' and character <= 'Z');
        const auto asciiDigit = character >= '0' and character <= '9';

        if (not asciiLetter and not asciiDigit and character != '_')
        {
            return false;
        }
    }

    return true;
}
} // namespace orm::db::detail
