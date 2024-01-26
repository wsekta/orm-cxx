#pragma once

#include <utility>

namespace orm::utils
{
template <auto Start, auto End, auto Inc, class F, class... Args>
constexpr auto constexpr_for(F&& f, Args... args) -> void
{
    if constexpr (Start < End)
    {
        f(std::integral_constant<decltype(Start), Start>{}, std::forward<Args>(args)...);
        constexpr_for<Start + Inc, End, Inc>(std::forward<F>(f), std::forward<Args>(args)...);
    }
}

template <class Tuple, class F, class... Args>
constexpr auto constexpr_for_tuple(Tuple&& t, F&& f, Args... args) -> void
{
    constexpr_for<std::size_t{0}, std::tuple_size_v<std::decay_t<Tuple>>, std::size_t{1}>(
        [&t, &f, &args...](auto I) { f(I, std::get<I>(std::forward<Tuple>(t)), std::forward<Args>(args)...); });
}
} // namespace orm::utils
