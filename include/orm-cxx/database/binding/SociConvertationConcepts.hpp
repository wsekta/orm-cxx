#pragma once

#include <type_traits>

namespace orm::db::binding
{
template <typename ModelField, typename... Types>
concept SociConvertable = (std::is_same_v<ModelField, Types> || ...);

template <typename ModelField>
concept SociConvertableToInt =
    SociConvertable<ModelField, bool, int8_t, char, unsigned char, short, unsigned short, long>;

template <typename ModelField>
concept SociConvertableToUnsignedLongLong = SociConvertable<ModelField, unsigned int, unsigned long>;

template <typename ModelField>
concept SociConvertableToDouble = SociConvertable<ModelField, float>;
} // namespace orm::db::binding
