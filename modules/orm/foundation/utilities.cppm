module;

#include <cstddef>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

export module orm:foundation_utilities;

// utils/ConstexprFor.hpp
namespace orm::utils
{
export
{

    template <auto Start, auto End, auto Inc, class F, class... Args>
    constexpr auto constexpr_for(F && f, Args... args) -> void
    {
        if constexpr (Start < End)
        {
            f(std::integral_constant<decltype(Start), Start>{}, std::forward<Args>(args)...);
            constexpr_for<Start + Inc, End, Inc>(std::forward<F>(f), std::forward<Args>(args)...);
        }
    }

    template <class Tuple, class F, class... Args>
    constexpr auto constexpr_for_tuple(Tuple && t, F && f, Args... args) -> void
    {
        constexpr_for<std::size_t{0}, std::tuple_size_v<std::decay_t<Tuple>>, std::size_t{1}>(
            [&t, &f, &args...](auto I) { f(I, std::get<I>(std::forward<Tuple>(t)), std::forward<Args>(args)...); });
    }

    template <class Tuple, class F, class... Args>
    constexpr auto constexpr_for_tuple(F && f, Args... args) -> void
    {
        constexpr_for<std::size_t{0}, std::tuple_size_v<std::decay_t<Tuple>>, std::size_t{1}>(
            [&f, &args...](auto I)
            {
                using field_t = std::tuple_element_t<I, std::decay_t<Tuple>>;
                f(I, static_cast<field_t>(nullptr), std::forward<Args>(args)...);
            });
    }
}
} // namespace orm::utils

// utils/StringUtils.hpp
namespace orm::utils
{
export
{

    constexpr auto replaceAll(std::string & text, const std::string& toReplace, const std::string& replaceWith) -> void
    {
        if (toReplace.empty())
        {
            return;
        }

        size_t start_pos = 0;

        while ((start_pos = text.find(toReplace, start_pos)) != std::string::npos)
        {
            text.replace(start_pos, toReplace.length(), replaceWith);
            start_pos += replaceWith.length();
        }
    }

    auto removeLastComma(std::string & text)->void;
}
}
