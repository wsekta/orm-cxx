#pragma once

#include <string>
#include <unordered_map>

namespace orm::model
{
template <typename T>
auto getColumnsNamesMapping() -> std::unordered_map<std::string, std::string>
{
    if constexpr (requires {
                      T::columns_names;
                      T::columns_names.contains(std::string{});
                      T::columns_names.at(std::string{});
                  })
    {
        return {T::columns_names.begin(), T::columns_names.end()};
    }
    else
    {
        return {};
    }
}

template <typename T>
auto getColumnName(const std::string& name) -> const std::string&
{
    static auto mapping = getColumnsNamesMapping<T>();

    if (mapping.contains(name))
    {
        return mapping.at(name);
    }

    return name;
}
} // namespace orm::model
