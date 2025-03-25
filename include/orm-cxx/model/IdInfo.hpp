#pragma once

#include <string>
#include <unordered_set>

#include "NameMapping.hpp"

namespace orm::model
{
template <typename T>
auto getPrimaryIdColumnsNames() -> std::unordered_set<std::string>
{
    if constexpr (requires { T::id_columns; })
    {
        std::unordered_set<std::string> ids{};

        for (const auto& id : T::id_columns)
        {
            ids.insert(getColumnName<T>(id));
        }

        return ids;
    }
    else if constexpr (requires(T t) { t.id; })
    {
        return {getColumnName<T>("id")};
    }
    else
    {
        return {};
    }
}

template <typename T>
consteval auto checkIfIsModelWithId() -> bool
{
    if constexpr (requires { T::id_columns; })
    {
        return true;
    }
    else if constexpr (requires(T t) { t.id; })
    {
        return true;
    }
    else
    {
        return false;
    }
}
} // namespace orm::model
