#pragma once

#include <rfl.hpp>
#include <source_location>
#include <string>
#include <utility>

#include "orm-cxx/utils/StringUtils.hpp"

namespace orm::model
{
/**
 * @brief Get the table name for a struct.
 *
 * @tparam T The type of the struct.
 * @return The table name as a string view.
 */
template <typename T>
auto getTableName() -> std::string
{
    if constexpr (requires { T::table_name; })
    {
        return std::string(T::table_name);
    }

    auto typeName = rfl::type_name_t<T>().str();

    utils::replaceAll(typeName, "::", "_");
    utils::replaceAll(typeName, "class ", "");
    utils::replaceAll(typeName, "struct ", "");

    return typeName;
}
}