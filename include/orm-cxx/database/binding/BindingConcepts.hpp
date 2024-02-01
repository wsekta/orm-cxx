#pragma once

#include <type_traits>

namespace orm::db::binding
{
template <typename ModelField, typename... Types>
concept IsOneOfTypes = (std::is_same_v<ModelField, Types> || ...);

template <typename ModelField>
concept SociConvertableToInt = IsOneOfTypes<ModelField, bool, int8_t, char, unsigned char, short, unsigned short, long>;

template <typename ModelField>
concept SociConvertableToUnsignedLongLong = IsOneOfTypes<ModelField, unsigned int, unsigned long>;

template <typename ModelField>
concept SociConvertableToDouble = IsOneOfTypes<ModelField, float>;

template <typename ModelField>
concept SociDefaultSupported = IsOneOfTypes<ModelField, int, unsigned long long, double, std::string>;
} // namespace orm::db::binding
