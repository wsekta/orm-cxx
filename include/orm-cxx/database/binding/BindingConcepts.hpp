#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>

namespace orm::db::binding
{
template <typename ModelField, typename... Types>
concept IsOneOfTypes = (std::is_same_v<ModelField, Types> || ...);

template <typename ModelField>
concept SociConvertableToInt =
    IsOneOfTypes<ModelField, bool, std::int8_t, char, unsigned char, short, unsigned short> or
    (std::is_same_v<ModelField, long> and sizeof(long) <= sizeof(int));

template <typename ModelField>
concept SociConvertableToLongLong = std::is_same_v<ModelField, long> and sizeof(long) > sizeof(int);

template <typename ModelField>
concept SociConvertableToUnsignedLongLong = IsOneOfTypes<ModelField, unsigned int, unsigned long>;

template <typename ModelField>
concept SociConvertableToDouble = IsOneOfTypes<ModelField, float>;

template <typename ModelField>
concept SociDefaultSupported = IsOneOfTypes<ModelField, int, long long, unsigned long long, double, std::string>;

template <typename T>
struct OptionalValue
{
    using Type = T;
};

template <typename T>
struct OptionalValue<std::optional<T>>
{
    using Type = T;
};

template <typename T>
using optional_value_t = typename OptionalValue<std::remove_cv_t<T>>::Type;

template <typename SchemaType, typename ModelField>
concept SchemaModel = SchemaType::template contains<std::remove_cv_t<optional_value_t<ModelField>>>;
} // namespace orm::db::binding
