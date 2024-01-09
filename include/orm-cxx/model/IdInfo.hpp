#pragma once

#include <string>
#include <unordered_set>

namespace orm::model
{
template <typename T>
auto getIdColumnsNames() -> std::unordered_set<std::string>
{
    if constexpr (requires { T::id_columns; })
    {
        return {T::id_columns.begin(), T::id_columns.end()};
    }
    else if constexpr (requires(T t) { t.id; })
    {
        return {"id"};
    }
    else
    {
        return {};
    }
}
}