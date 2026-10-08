#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <limits>
#include <optional>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include "orm-cxx/model/StaticModel.hpp"
#include "QueryValue.hpp"

namespace orm::query
{
template <typename T, std::size_t Index>
class Parameter;
}

namespace orm::query::detail
{
struct TypedAccess
{
    template <typename T, typename... Args>
    static constexpr auto make(Args&&... args) -> T
    {
        return T(std::forward<Args>(args)...);
    }
    template <typename T>
    static auto erase(const T& value)
    {
        return value.runtime();
    }
    template <typename T, typename Args>
    static auto erase(const T& value, const Args& args)
    {
        if constexpr (requires { value.runtime(args); })
            return value.runtime(args);
        else
            return value.runtime();
    }
    template <typename T>
    static constexpr auto children(const T& value) -> const auto&
    {
        return value.children;
    }
};

template <typename T>
auto erase(const T& value)
{
    return TypedAccess::erase(value);
}
template <typename T>
using scalar_t = std::remove_cv_t<model::detail::static_optional_value_t<std::remove_cvref_t<T>>>;
template <typename T>
inline constexpr bool numericScalar =
    std::is_arithmetic_v<std::remove_cvref_t<T>> && !std::same_as<std::remove_cvref_t<T>, bool> &&
    requires { model::LogicalTypeTraits<std::remove_cvref_t<T>>::value; };
template <typename T>
inline constexpr bool supportedScalar =
    numericScalar<T> || std::same_as<std::remove_cvref_t<T>, bool> || std::same_as<std::remove_cvref_t<T>, std::string>;
template <typename Target, typename Source>
inline constexpr bool fitsNumericLimits = []
{
    using T = std::remove_cvref_t<Target>;
    using S = std::remove_cvref_t<Source>;
    if constexpr (!std::is_arithmetic_v<T> || std::same_as<T, bool> || !std::is_arithmetic_v<S> ||
                  std::same_as<S, bool>)
        return false;
    else if constexpr (std::is_integral_v<T> && std::is_integral_v<S>)
        return (!std::is_signed_v<S> || std::is_signed_v<T>) &&
               std::numeric_limits<T>::digits >= std::numeric_limits<S>::digits;
    else if constexpr (std::is_floating_point_v<T> && std::is_integral_v<S>)
        return std::numeric_limits<T>::radix == 2 && std::numeric_limits<T>::digits >= std::numeric_limits<S>::digits &&
               std::numeric_limits<T>::max_exponent > std::numeric_limits<S>::digits;
    else if constexpr (std::is_floating_point_v<T> && std::is_floating_point_v<S>)
        return std::numeric_limits<T>::radix == std::numeric_limits<S>::radix &&
               std::numeric_limits<T>::digits >= std::numeric_limits<S>::digits &&
               std::numeric_limits<T>::max_exponent >= std::numeric_limits<S>::max_exponent &&
               std::numeric_limits<T>::min_exponent <= std::numeric_limits<S>::min_exponent;
    else
        return false;
}();
template <typename Target, typename Source>
inline constexpr bool isSafeNumericWidening = numericScalar<Target> && fitsNumericLimits<Target, Source>;
template <typename T>
inline constexpr bool stringValue =
    std::same_as<std::remove_cvref_t<T>, std::string> || std::same_as<std::remove_cvref_t<T>, std::string_view> ||
    std::same_as<std::decay_t<T>, const char*> || std::same_as<std::decay_t<T>, char*>;
template <typename T>
struct ParameterValue
{
    using type = std::remove_cvref_t<T>;
};
template <typename T, std::size_t I>
struct ParameterValue<Parameter<T, I>>
{
    using type = T;
};
template <typename T>
using parameter_value_t = typename ParameterValue<std::remove_cvref_t<T>>::type;
template <typename Target, typename Source>
inline constexpr bool compatibleValue = isSafeNumericWidening<Target, parameter_value_t<Source>> ||
                                        (std::same_as<Target, bool> && std::same_as<parameter_value_t<Source>, bool>) ||
                                        (std::same_as<Target, std::string> && stringValue<parameter_value_t<Source>>);
template <typename Target, typename Source>
concept ORM_QUERY_UNBOUND_PARAMETER_VALUE = std::same_as<parameter_value_t<Source>, std::remove_cvref_t<Source>>;
template <typename Target, typename Source>
concept ORM_QUERY_VALUE = compatibleValue<Target, Source> && ORM_QUERY_UNBOUND_PARAMETER_VALUE<Target, Source>;
template <typename Target, typename Source>
concept ORM_QUERY_VALUE_OR_PARAMETER = compatibleValue<Target, Source>;
template <typename Model, typename Expected>
concept ORM_QUERY_MODEL_TYPE = std::same_as<Model, Expected>;
template <typename E, typename M>
concept ORM_QUERY_MODEL = requires { typename E::Model; } && ORM_QUERY_MODEL_TYPE<typename E::Model, M>;
template <auto... Members>
concept ORM_QUERY_MEMBER = sizeof...(Members) > 0 && (std::is_member_object_pointer_v<decltype(Members)> && ...) &&
                           ((Members != nullptr) && ...);
template <typename T>
concept ORM_QUERY_NUMERIC = numericScalar<T>;
template <typename T>
concept ORM_QUERY_STRING = std::same_as<T, std::string>;
template <typename T>
concept ORM_QUERY_ORDERABLE = numericScalar<T> || std::same_as<T, std::string>;
template <bool Nullable>
concept ORM_QUERY_NULLABLE = Nullable;

template <typename Target, typename Source>
    requires ORM_QUERY_VALUE<Target, Source>
auto typedValue(Source&& value) -> QueryValue
{
    if constexpr (std::same_as<Target, std::string>)
    {
        if constexpr (std::is_pointer_v<std::decay_t<Source>>)
            if (value == nullptr)
                throw std::invalid_argument{"A string query value must not be a null pointer"};
        return QueryValue{std::string{std::forward<Source>(value)}};
    }
    else
        return QueryValue{static_cast<Target>(value)};
}

template <typename... Tuples>
using ConcatTuples = decltype(std::tuple_cat(std::declval<Tuples>()...));
template <typename T, typename = void>
struct ParameterTypesTrait
{
    using type = std::tuple<>;
};
template <typename T>
struct ParameterTypesTrait<T, std::void_t<typename T::ParameterTypes>>
{
    using type = typename T::ParameterTypes;
};
template <typename... T>
struct ParameterTypesTrait<std::tuple<T...>, void>
{
    using type = ConcatTuples<typename ParameterTypesTrait<std::remove_cvref_t<T>>::type...>;
};
template <typename T>
using ParameterTypes = typename ParameterTypesTrait<std::remove_cvref_t<T>>::type;
template <typename T>
inline constexpr bool hasParameters = std::tuple_size_v<ParameterTypes<T>> != 0;
template <typename T>
concept ORM_QUERY_UNBOUND_PARAMETER = !hasParameters<T>;
template <typename T>
concept ORM_QUERY_BOUND = ORM_QUERY_UNBOUND_PARAMETER<T>;
template <typename Tuple>
struct SlotIndices;
template <typename... P>
struct SlotIndices<std::tuple<P...>>
{
    inline static constexpr std::array<std::size_t, sizeof...(P)> value{P::index...};
};
template <typename T>
inline constexpr auto parameterSlots = SlotIndices<ParameterTypes<T>>::value;

template <std::size_t N>
struct CapturedText
{
    std::array<char, N> data{};
    std::size_t length{};
    constexpr auto view() const -> std::string_view
    {
        return {data.data(), length};
    }
};
template <typename T>
constexpr auto captureValue(T&& value)
{
    using V = std::remove_cvref_t<T>;
    if constexpr (std::is_array_v<V> && std::same_as<std::remove_cv_t<std::remove_extent_t<V>>, char>)
    {
        CapturedText<std::extent_v<V>> result;
        for (; result.length < result.data.size() && value[result.length] != '\0'; ++result.length)
            result.data[result.length] = value[result.length];
        return result;
    }
    else if constexpr (stringValue<T>)
    {
        if constexpr (std::is_pointer_v<std::decay_t<T>>)
            if (value == nullptr)
                throw std::invalid_argument{"A string query value must not be a null pointer"};
        return std::string{std::forward<T>(value)};
    }
    else if constexpr (model::isNullable<V>)
    {
        using Owned = decltype(captureValue(value.value()));
        if (value.has_value())
            return std::optional<Owned>{captureValue(value.value())};
        return std::optional<Owned>{};
    }
    else
        return V{std::forward<T>(value)};
}
template <typename T>
struct IsParameter : std::false_type
{
};
template <typename T, std::size_t I>
struct IsParameter<Parameter<T, I>> : std::true_type
{
};
template <typename T>
inline constexpr bool isParameter = IsParameter<std::remove_cvref_t<T>>::value;
template <typename T, typename Args>
constexpr decltype(auto) resolveValue(const T& value, const Args& args)
{
    if constexpr (isParameter<T>)
        return std::get<T::index>(args);
    else if constexpr (requires { value.view(); })
        return value.view();
    else
        return (value);
}

template <typename T>
struct ContainerTraits
{
    inline static constexpr bool isContainer = false;
};
template <typename T, std::size_t N>
struct ContainerTraits<std::array<T, N>>
{
    using Value = T;
    inline static constexpr bool isContainer = true;
    inline static constexpr bool fixed = true;
    inline static constexpr int family = 0;
    inline static constexpr std::size_t size = N;
};
template <typename T, typename A>
struct ContainerTraits<std::vector<T, A>>
{
    using Value = T;
    inline static constexpr bool isContainer = true;
    inline static constexpr bool fixed = false;
    inline static constexpr int family = 1;
};
template <typename T>
struct ContainerTraits<std::initializer_list<T>>
{
    using Value = T;
    inline static constexpr bool isContainer = true;
    inline static constexpr bool fixed = false;
    inline static constexpr int family = 2;
};
template <typename Target, typename Source>
consteval auto parameterCompatible() -> bool
{
    using T = std::remove_cvref_t<Target>;
    using S = std::remove_cvref_t<Source>;
    if constexpr (ContainerTraits<T>::isContainer && ContainerTraits<S>::isContainer)
    {
        if constexpr (ContainerTraits<T>::family != ContainerTraits<S>::family)
            return false;
        else if constexpr (ContainerTraits<T>::fixed)
            return ContainerTraits<T>::size == ContainerTraits<S>::size &&
                   compatibleValue<typename ContainerTraits<T>::Value, typename ContainerTraits<S>::Value>;
        else
            return compatibleValue<typename ContainerTraits<T>::Value, typename ContainerTraits<S>::Value>;
    }
    else if constexpr (model::isNullable<T>)
    {
        if constexpr (model::isNullable<S>)
            return parameterCompatible<scalar_t<T>, scalar_t<S>>();
        else
            return parameterCompatible<scalar_t<T>, S>();
    }
    else if constexpr (std::is_arithmetic_v<T> && !std::same_as<T, bool>)
        return fitsNumericLimits<T, S>;
    else if constexpr (stringValue<T>)
        return stringValue<S>;
    else
        return compatibleValue<T, S>;
}
template <typename P, typename Slots>
struct SlotTypesCompatible;
template <typename P, typename... Slots>
struct SlotTypesCompatible<P, std::tuple<Slots...>>
{
    inline static constexpr bool value =
        ((P::index != Slots::index || std::same_as<typename P::Value, typename Slots::Value>) && ...);
};
template <typename Root, typename... Args>
consteval auto validateParameters() -> void
{
    using Slots = ParameterTypes<Root>;
    constexpr auto slots = parameterSlots<Root>;
    constexpr bool indicesInRange = [slots]() consteval
    {
        for (const auto slot : slots)
            if (slot >= slots.size())
                return false;
        return true;
    }();
    constexpr auto count = [slots]() consteval
    {
        std::size_t result = 0;
        for (const auto slot : slots)
        {
            if (slot >= slots.size())
                return std::size_t{0};
            result = std::max(result, slot + 1);
        }
        return result;
    }();
    constexpr bool continuous = [slots]() consteval
    {
        if (!indicesInRange)
            return false;
        for (std::size_t i = 0; i < count; ++i)
            if (std::ranges::find(slots, i) == slots.end())
                return false;
        return true;
    }();
    static_assert(continuous, "ORM_QUERY_PARAMETER_INDEX: parameter indices must be continuous from zero");
    static_assert(sizeof...(Args) == count, "ORM_QUERY_PARAMETER_COUNT: wrong number of bound arguments");
    constexpr bool typesMatch = []<typename... P>(std::tuple<P...>*) consteval
    { return (SlotTypesCompatible<P, Slots>::value && ...); }(static_cast<Slots*>(nullptr));
    static_assert(typesMatch, "ORM_QUERY_PARAMETER_TYPE: one index must declare exactly one type");
    if constexpr (continuous && typesMatch && sizeof...(Args) == count)
    {
        using Values = std::tuple<Args...>;
        constexpr bool compatible = []<std::size_t... I>(std::index_sequence<I...>) consteval
        {
            return (parameterCompatible<typename std::tuple_element_t<I, Slots>::Value,
                                        std::tuple_element_t<std::tuple_element_t<I, Slots>::index, Values>>() &&
                    ...);
        }(std::make_index_sequence<std::tuple_size_v<Slots>>{});
        static_assert(compatible, "ORM_QUERY_PARAMETER_VALUE: bound argument cannot safely fit its declared type");
    }
}
} // namespace orm::query::detail

namespace orm::query
{
template <typename T, std::size_t Index>
class Parameter
{
public:
    using Value = T;
    using ParameterTypes = std::tuple<Parameter>;
    inline static constexpr std::size_t index = Index;
    inline static constexpr std::array<std::size_t, 1> parameterSlots{Index};

private:
    friend struct detail::TypedAccess;
    constexpr Parameter() = default;
};
template <typename T, std::size_t Index>
constexpr auto param() -> Parameter<T, Index>
{
    return detail::TypedAccess::make<Parameter<T, Index>>();
}
} // namespace orm::query
