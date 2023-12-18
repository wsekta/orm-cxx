#pragma once

#include <source_location>
#include <string>
#include <utility>

#include "rfl.hpp"

namespace orm::model
{
/**
 * @brief Get the table name for a struct.
 *
 * @tparam T The type of the struct.
 * @return The table name as a string view.
 */
template <typename T>
auto tableName() -> std::string
{
    if constexpr (requires(T t) { t.table_name; })
    {
        return std::string(T::table_name);
    }

    auto typeName = rfl::type_name_t<T>().str();
    
    std::string del = " ";

    auto iter = typeName.find(del);

    if (iter != std::string::npos)
    {
        return typeName.substr(iter + del.size());
    }

    return typeName;
}
}