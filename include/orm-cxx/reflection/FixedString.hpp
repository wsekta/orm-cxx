#pragma once

#include <cstddef>
#include <string_view>

namespace orm::reflection
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
