#pragma once

#include <source_location>
#include <string_view>

namespace orm::reflection
{
template <typename T>
consteval auto getTypeName() -> std::string_view
{
    const auto func_name = std::string_view{std::source_location::current().function_name()};
#if defined(__clang__)
    const auto split = func_name.substr(0, func_name.size() - 1);
    return split.substr(split.find("T = ") + 4);
#elif defined(__GNUC__)
    const auto split = func_name.substr(0, func_name.find(';'));
    return split.substr(split.find("T = ") + 4);
#elif defined(_MSC_VER)
    const auto split = func_name.substr(0, func_name.size() - 7);
    return split.substr(split.find("getTypeName<") + 12);
#else
    static_assert(false, "You are using an unsupported compiler. Please use GCC, Clang "
                         "or MSVC or explicitly tag your structs using 'Tag' or 'Name'.");
#endif
}
} // namespace orm::reflection
