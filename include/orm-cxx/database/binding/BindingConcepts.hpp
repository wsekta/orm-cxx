#pragma once

#include <string>
#include <type_traits>

#include "orm-cxx/model/IdInfo.hpp"

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
concept SociDefaultSupported = IsOneOfTypes<ModelField, int, long long, unsigned long long, double, std::string>;

template <typename ModelField>
concept ModelWithId = orm::model::checkIfIsModelWithId<ModelField>();
} // namespace orm::db::binding
