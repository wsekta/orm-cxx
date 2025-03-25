#pragma once

#include <string>
#include <string_view>
#include <array>

namespace orm::utils
{
template <typename T>
concept StringCallable = requires(T t) { t(); } && std::is_convertible_v<std::invoke_result_t<T>, std::string>;

struct OversizedArray
{
    std::array<char, 1024 * 1024> data{};
    std::size_t size{};
};

constexpr auto makeOversizedArray(const std::string& str)
{
    OversizedArray result;
    std::copy(str.begin(), str.end(), result.data.begin());
    result.size = str.size();
    return result;
}

consteval auto makeRightSizedArray(auto callable)
{
    constexpr auto oversized = makeOversizedArray(callable());
    std::array<char, oversized.size> result;
    std::copy(oversized.data.begin(), oversized.data.begin() + oversized.size, result.begin());
    return result;
}

template<auto Data>
consteval const auto& makeStatic()
{
    return Data;
}

consteval auto makeConstexprStringView(auto callable) -> std::string_view
{
    constexpr auto& data = makeStatic<makeRightSizedArray(callable)>();
    return std::string_view{data.begin(), data.size()};
}
} // namespace orm::utils