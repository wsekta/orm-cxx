#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include "ColumnType.hpp"
#include "orm-cxx/reflection/Reflection.hpp"
#include "orm-cxx/relations.hpp"

namespace orm::model
{
namespace detail
{
template <typename>
inline constexpr bool alwaysFalse = false;

template <typename T>
struct StaticOptionalTraits
{
    inline static constexpr bool isOptional = false;
    using Value = T;
};

template <typename T>
struct StaticOptionalTraits<std::optional<T>>
{
    inline static constexpr bool isOptional = true;
    using Value = T;
};

template <typename T>
using static_optional_value_t = typename StaticOptionalTraits<std::remove_cv_t<T>>::Value;

template <typename T>
struct MemberPointerTraits;

template <typename Value, typename Owner>
struct MemberPointerTraits<Value Owner::*>
{
    using OwnerType = Owner;
    using ValueType = Value;
};

template <auto Member>
using member_owner_t = typename MemberPointerTraits<std::remove_cv_t<decltype(Member)>>::OwnerType;

template <auto Member>
using member_value_t = typename MemberPointerTraits<std::remove_cv_t<decltype(Member)>>::ValueType;

template <auto Member>
inline constexpr bool isPersistentColumnMember = !orm::is_relation_collection_v<member_value_t<Member>> &&
                                                 !orm::is_optional_relation_collection_v<member_value_t<Member>>;

template <auto Member>
inline constexpr auto reflectedMemberNameStorage = reflection::memberName<Member>();

template <auto Member>
consteval decltype(auto) reflectedMemberName()
{
    return (reflectedMemberNameStorage<Member>);
}

template <typename Model, std::size_t Index>
consteval auto reflectedFieldName() -> std::string_view
{
    constexpr auto descriptors = reflection::fields<Model>();
    return descriptors[Index].name;
}

template <typename Model, auto Member, std::size_t... Indices>
consteval auto memberExistsImpl(std::index_sequence<Indices...>) -> bool
{
    constexpr auto memberName = reflectedMemberName<Member>();
    return ((memberName.view() == reflectedFieldName<Model, Indices>()) || ...);
}

template <typename Model, auto Member>
consteval auto memberExists() -> bool
{
    return memberExistsImpl<Model, Member>(std::make_index_sequence<reflection::fieldCount<Model>>{});
}

template <typename Model, auto Member, std::size_t... Indices>
consteval auto memberIndexImpl(std::index_sequence<Indices...>) -> std::size_t
{
    constexpr auto memberName = reflectedMemberName<Member>();
    constexpr std::array<bool, sizeof...(Indices)> matches{
        (memberName.view() == reflectedFieldName<Model, Indices>())...};
    for (std::size_t index = 0; index < matches.size(); ++index)
    {
        if (matches[index])
        {
            return index;
        }
    }
    return reflection::fieldCount<Model>;
}

template <typename Model, auto Member>
consteval auto memberIndex() -> std::size_t
{
    constexpr auto result = memberIndexImpl<Model, Member>(std::make_index_sequence<reflection::fieldCount<Model>>{});
    static_assert(result < reflection::fieldCount<Model>,
                  "ORM_MODEL_MEMBER: a mapped member is not an aggregate field");
    return result;
}

template <auto... Members>
consteval auto uniqueMemberNames() -> bool
{
    const auto nameStorage = std::tuple{reflectedMemberName<Members>()...};
    const auto names =
        std::apply([](const auto&... name) { return std::array<std::string_view, sizeof...(Members)>{name.view()...}; },
                   nameStorage);
    for (std::size_t left = 0; left < names.size(); ++left)
    {
        for (std::size_t right = left + 1; right < names.size(); ++right)
        {
            if (names[left] == names[right])
            {
                return false;
            }
        }
    }
    return true;
}
} // namespace detail

/**
 * @brief Compile-time override of one aggregate member's SQL column name.
 */
template <auto Member, reflection::FixedString SqlName>
    requires std::is_member_object_pointer_v<decltype(Member)>
struct ColumnName
{
    inline static constexpr auto member = Member;
    inline static constexpr auto sqlName = SqlName;
};

/**
 * @brief Creates a typed SQL column-name override.
 */
template <auto Member, reflection::FixedString SqlName>
    requires std::is_member_object_pointer_v<decltype(Member)>
consteval auto columnName() -> ColumnName<Member, SqlName>
{
    static_assert(not SqlName.view().empty(), "ORM_MODEL_COLUMN_MAPPING: a mapped SQL column name must not be empty");
    return {};
}

template <typename... Definitions>
struct ColumnNames
{
    static_assert((requires {
                      Definitions::member;
                      Definitions::sqlName;
                  } && ...),
                  "ORM_MODEL_COLUMN_MAPPING: columnNames() accepts only columnName<Member, SqlName>() definitions");

    template <typename Model>
    [[nodiscard]] static consteval auto isValidFor() -> bool
    {
        if constexpr (not(std::same_as<detail::member_owner_t<Definitions::member>, Model> && ...))
        {
            return false;
        }
        else if constexpr (not(detail::memberExists<Model, Definitions::member>() && ...))
        {
            return false;
        }
        else if constexpr (not(detail::isPersistentColumnMember<Definitions::member> && ...))
        {
            return false;
        }
        else if constexpr (not detail::uniqueMemberNames<Definitions::member...>())
        {
            return false;
        }
        else
        {
            return ((not Definitions::sqlName.view().empty()) && ...);
        }
    }

    [[nodiscard]] consteval auto find(std::string_view memberName) const -> std::optional<std::string_view>
    {
        std::optional<std::string_view> result;
        (
            [&]
            {
                if (detail::reflectedMemberName<Definitions::member>() == memberName)
                {
                    result = Definitions::sqlName.view();
                }
            }(),
            ...);
        return result;
    }
};

/**
 * @brief Groups typed SQL column-name overrides.
 */
template <typename... Definitions>
consteval auto columnNames(Definitions...) -> ColumnNames<Definitions...>
{
    return {};
}

template <auto... Members>
    requires(std::is_member_object_pointer_v<decltype(Members)> && ...)
struct PrimaryKey
{
    inline static constexpr std::size_t size = sizeof...(Members);

    template <typename Model>
    [[nodiscard]] static consteval auto isValidFor() -> bool
    {
        if constexpr (sizeof...(Members) == 0)
        {
            return true;
        }
        else
        {
            return (std::same_as<detail::member_owner_t<Members>, Model> && ...) &&
                   (detail::memberExists<Model, Members>() && ...) &&
                   (detail::isPersistentColumnMember<Members> && ...) && detail::uniqueMemberNames<Members...>();
        }
    }

    [[nodiscard]] consteval auto contains(std::string_view memberName) const -> bool
    {
        if constexpr (sizeof...(Members) == 0)
        {
            return false;
        }
        else
        {
            const auto nameStorage = std::tuple{detail::reflectedMemberName<Members>()...};
            return std::apply([&memberName](const auto&... name) { return ((name.view() == memberName) || ...); },
                              nameStorage);
        }
    }

    template <typename Model>
    [[nodiscard]] static consteval auto indices()
    {
        return std::array<std::size_t, sizeof...(Members)>{detail::memberIndex<Model, Members>()...};
    }
};

/**
 * @brief Defines an ordered, typed primary key. An empty definition makes the
 * model explicitly keyless.
 */
template <auto... Members>
    requires(std::is_member_object_pointer_v<decltype(Members)> && ...)
consteval auto primaryKey() -> PrimaryKey<Members...>
{
    return {};
}

template <auto... Members>
    requires(std::is_member_object_pointer_v<decltype(Members)> && ...)
struct AutoIncrement
{
    inline static constexpr std::size_t size = sizeof...(Members);

    template <typename Model>
    [[nodiscard]] static consteval auto isValidFor() -> bool
    {
        if constexpr (sizeof...(Members) == 0)
        {
            return true;
        }
        else
        {
            return (std::same_as<detail::member_owner_t<Members>, Model> && ...) &&
                   (detail::memberExists<Model, Members>() && ...) &&
                   (detail::isPersistentColumnMember<Members> && ...) && detail::uniqueMemberNames<Members...>();
        }
    }

    [[nodiscard]] consteval auto contains(std::string_view memberName) const -> bool
    {
        if constexpr (sizeof...(Members) == 0)
        {
            return false;
        }
        else
        {
            const auto nameStorage = std::tuple{detail::reflectedMemberName<Members>()...};
            return std::apply([&memberName](const auto&... name) { return ((name.view() == memberName) || ...); },
                              nameStorage);
        }
    }
};

/**
 * @brief Defines typed auto-increment members. Static-model validation further
 * restricts this to one non-null int primary-key member.
 */
template <auto... Members>
    requires(std::is_member_object_pointer_v<decltype(Members)> && ...)
consteval auto autoIncrement() -> AutoIncrement<Members...>
{
    return {};
}

template <typename T>
struct LogicalTypeTraits;

template <>
struct LogicalTypeTraits<bool>
{
    inline static constexpr auto value = ColumnType::Bool;
};

template <>
struct LogicalTypeTraits<char>
{
    inline static constexpr auto value = ColumnType::Char;
};

template <>
struct LogicalTypeTraits<signed char>
{
    inline static constexpr auto value = ColumnType::Char;
};

template <>
struct LogicalTypeTraits<unsigned char>
{
    inline static constexpr auto value = ColumnType::UnsignedChar;
};

template <>
struct LogicalTypeTraits<short>
{
    inline static constexpr auto value = ColumnType::Short;
};

template <>
struct LogicalTypeTraits<unsigned short>
{
    inline static constexpr auto value = ColumnType::UnsignedShort;
};

template <>
struct LogicalTypeTraits<int>
{
    inline static constexpr auto value = ColumnType::Int;
};

template <>
struct LogicalTypeTraits<unsigned int>
{
    inline static constexpr auto value = ColumnType::UnsignedInt;
};

template <>
struct LogicalTypeTraits<long>
{
    inline static constexpr auto value = sizeof(long) > sizeof(int) ? ColumnType::LongLong : ColumnType::Int;
};

template <>
struct LogicalTypeTraits<unsigned long>
{
    inline static constexpr auto
        value = sizeof(unsigned long) > sizeof(unsigned int) ? ColumnType::UnsignedLongLong : ColumnType::UnsignedInt;
};

template <>
struct LogicalTypeTraits<long long>
{
    inline static constexpr auto value = ColumnType::LongLong;
};

template <>
struct LogicalTypeTraits<unsigned long long>
{
    inline static constexpr auto value = ColumnType::UnsignedLongLong;
};

template <>
struct LogicalTypeTraits<float>
{
    inline static constexpr auto value = ColumnType::Float;
};

template <>
struct LogicalTypeTraits<double>
{
    inline static constexpr auto value = ColumnType::Double;
};

template <>
struct LogicalTypeTraits<std::string>
{
    inline static constexpr auto value = ColumnType::String;
};

/**
 * @brief Resolves a C++ field type to its logical database type at compile
 * time. Users may extend the closed set through LogicalTypeTraits<T>.
 */
template <typename T>
consteval auto logicalType() -> ColumnType
{
    using field_t = std::remove_cvref_t<T>;
    using value_t = std::remove_cv_t<detail::static_optional_value_t<field_t>>;

    if constexpr (requires { LogicalTypeTraits<value_t>::value; })
    {
        return LogicalTypeTraits<value_t>::value;
    }
    else
    {
        static_assert(detail::alwaysFalse<value_t>, "ORM_MODEL_LOGICAL_TYPE: unsupported model field type; specialize "
                                                    "orm::model::LogicalTypeTraits<T>.");
    }
}

template <typename T>
inline constexpr bool isNullable = detail::StaticOptionalTraits<std::remove_cvref_t<T>>::isOptional;
} // namespace orm::model

namespace orm
{
template <auto Member, reflection::FixedString SqlName>
    requires std::is_member_object_pointer_v<decltype(Member)>
consteval auto columnName()
{
    return model::columnName<Member, SqlName>();
}

template <typename... Definitions>
consteval auto columnNames(Definitions... definitions)
{
    return model::columnNames(definitions...);
}

template <auto... Members>
    requires(std::is_member_object_pointer_v<decltype(Members)> && ...)
consteval auto primaryKey()
{
    return model::primaryKey<Members...>();
}

template <auto... Members>
    requires(std::is_member_object_pointer_v<decltype(Members)> && ...)
consteval auto autoIncrement()
{
    return model::autoIncrement<Members...>();
}

template <typename T>
consteval auto logicalType() -> model::ColumnType
{
    return model::logicalType<T>();
}
} // namespace orm
