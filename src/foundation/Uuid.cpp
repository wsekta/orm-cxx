module;

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

module orm;

namespace
{
auto hexDigit(char character) -> unsigned int
{
    if (character >= '0' && character <= '9')
    {
        return static_cast<unsigned int>(character - '0');
    }
    if (character >= 'a' && character <= 'f')
    {
        return static_cast<unsigned int>(character - 'a') + 10U;
    }
    if (character >= 'A' && character <= 'F')
    {
        return static_cast<unsigned int>(character - 'A') + 10U;
    }
    throw std::invalid_argument{"UUID must use the 8-4-4-4-12 hexadecimal format"};
}
} // namespace

namespace orm
{
auto Uuid::parse(std::string_view text) -> Uuid
{
    if (text.size() != 36)
    {
        throw std::invalid_argument{"UUID must use the 8-4-4-4-12 hexadecimal format"};
    }

    std::array<std::byte, 16> parsed{};
    std::size_t position{};
    for (auto& byte : parsed)
    {
        if (position == 8 || position == 13 || position == 18 || position == 23)
        {
            if (text[position] != '-')
            {
                throw std::invalid_argument{"UUID must use the 8-4-4-4-12 hexadecimal format"};
            }
            ++position;
        }
        const auto high = hexDigit(text[position++]);
        const auto low = hexDigit(text[position++]);
        byte = static_cast<std::byte>((high << 4U) | low);
    }
    return Uuid{parsed};
}

auto Uuid::toString() const -> std::string
{
    constexpr std::string_view digits{"0123456789abcdef"};
    std::string text(36, '-');
    std::size_t position{};
    for (const auto byte : value)
    {
        if (position == 8 || position == 13 || position == 18 || position == 23)
        {
            ++position;
        }
        const auto number = std::to_integer<unsigned int>(byte);
        text[position++] = digits[number >> 4U];
        text[position++] = digits[number & 0x0fU];
    }
    return text;
}
} // namespace orm
