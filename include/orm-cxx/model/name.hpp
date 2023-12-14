#pragma once

#include <boost/pfr/core_name.hpp>
#include <string_view>
#include <source_location>
#include <string>
#include <utility>

namespace orm::model
{
template <class T>
consteval auto get_type_name_str_view() {
  const auto func_name =
      std::string_view{std::source_location::current().function_name()};
#if defined(__clang__)
  const auto split = func_name.substr(0, func_name.size() - 1);
  return split.substr(split.find("T = ") + 4);
#elif defined(__GNUC__)
  const auto split = func_name.substr(0, func_name.size() - 1);
  return split.substr(split.find("T = ") + 4);
#elif defined(_MSC_VER)
  const auto split = func_name.substr(0, func_name.size() - 7);
  return split.substr(split.find("get_type_name_str_view<") + 23);
#else
  static_assert(
      false,
      "You are using an unsupported compiler. Please use GCC, Clang "
      "or MSVC or explicitly tag your structs using 'Tag' or 'Name'.");
#endif
}

template <class T>
consteval auto get_type_name() {
  constexpr auto name = get_type_name_str_view<T>();
  const auto to_str_lit = [&]<auto... Ns>(std::index_sequence<Ns...>) {
    return std::string_view{name[0] , sizeof...(Ns) + 1};
  };
  return to_str_lit(std::make_index_sequence<name.size()>{});
}

/**
 * @brief Check if a struct has a table name.
 *
 * @tparam T The type of the struct.
 * @return True if the struct has a table name, false otherwise.
 */
template <typename T>
constexpr auto hasTableName() -> bool
{
    if constexpr (requires(T t) { t.table_name;})
    {
        return true;
    }
    else
    {
        return false;
    }
}

/**
 * @brief Get the table name for a struct.
 *
 * @tparam T The type of the struct.
 * @return The table name as a string view.
 */
template <typename T>
constexpr auto tableName() -> std::string_view
{
    if constexpr (hasTableName<T>())
    {
        return T::table_name;
    }

    return get_type_name_str_view<T>();
}
}