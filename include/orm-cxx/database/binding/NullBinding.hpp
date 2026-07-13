#pragma once

#include <string>
#include <string_view>

#include "soci/values.h"

namespace orm::db::binding
{
template <typename T>
inline auto bindTypedNull(soci::values& values, std::string_view name, const T& value) -> void
{
    const auto parameterName = std::string{name};

    // SOCI's initial values::set() conversion resets the indicator to i_ok.
    // Set the indicator only after the named value and its storage type exist.
    values.set(parameterName, value);
    values.set(parameterName, value, soci::i_null);
}
} // namespace orm::db::binding
