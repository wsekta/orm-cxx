#pragma once

#include <string>
#include <string_view>

#include "orm-cxx/utils/StringUtils.hpp"
#include "orm-cxx/utils/ConstexprStringView.hpp"
#include "orm-cxx/reflection/TypeName.hpp"

namespace orm::model
{
/**
 * @brief Get the table name for a struct.
 *
 * @tparam T The type of the struct.
 * @return The table name as a string view.
 */
template <typename T>
consteval auto getTableName() -> std::string_view
{
    if constexpr (requires { T::table_name; })
    {
        return T::table_name;
    }
    else
    {
        constexpr auto callable = []() {
            auto typeName = std::string(reflection::getTypeName<T>());

            utils::replaceAll(typeName, "::", "_");
            utils::replaceAll(typeName, "class ", "");
            utils::replaceAll(typeName, "struct ", "");

            return typeName;
        };

        return utils::makeConstexprStringView(callable);
    }
}
} // namespace orm::model
