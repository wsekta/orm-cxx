#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <memory>
#include <optional>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "orm-cxx/reflection/Reflection.hpp"

namespace orm
{
class DatabaseCore;
template <typename SchemaType>
class Database;

namespace detail
{
template <typename T>
class RelationCollection
{
public:
    using value_type = T;
    using container_type = std::vector<T>;
    using size_type = typename container_type::size_type;
    using iterator = typename container_type::iterator;
    using const_iterator = typename container_type::const_iterator;

    RelationCollection() = default;

    RelationCollection(const RelationCollection& other)
        : values_(other.values_ == nullptr ? nullptr : std::make_shared<container_type>(*other.values_)),
          loaded_(other.loaded_)
    {
    }

    auto operator=(const RelationCollection& other) -> RelationCollection&
    {
        if (this == &other)
        {
            return *this;
        }

        auto copiedValues = other.values_ == nullptr ? std::shared_ptr<container_type>{} :
                                                       std::make_shared<container_type>(*other.values_);
        values_ = std::move(copiedValues);
        loaded_ = other.loaded_;
        return *this;
    }

    RelationCollection(RelationCollection&&) noexcept = default;
    auto operator=(RelationCollection&&) noexcept -> RelationCollection& = default;

    explicit RelationCollection(container_type values)
        : values_(std::make_shared<container_type>(std::move(values))), loaded_(true)
    {
    }

    [[nodiscard]] auto isLoaded() const noexcept -> bool
    {
        return loaded_;
    }

    auto values() -> container_type&
    {
        return mutableValues();
    }

    auto values() const -> const container_type&
    {
        return readableValues();
    }

    auto begin() -> iterator
    {
        return values().begin();
    }

    auto begin() const -> const_iterator
    {
        return values().begin();
    }

    auto cbegin() const -> const_iterator
    {
        return values().cbegin();
    }

    auto end() -> iterator
    {
        return values().end();
    }

    auto end() const -> const_iterator
    {
        return values().end();
    }

    auto cend() const -> const_iterator
    {
        return values().cend();
    }

    [[nodiscard]] auto size() const noexcept -> size_type
    {
        return values_ == nullptr ? 0 : values_->size();
    }

    [[nodiscard]] auto empty() const noexcept -> bool
    {
        return values_ == nullptr or values_->empty();
    }

    auto operator[](size_type index) -> T&
    {
        return values()[index];
    }

    auto operator[](size_type index) const -> const T&
    {
        return values()[index];
    }

private:
    friend class orm::DatabaseCore;
    template <typename>
    friend class orm::Database;

    auto setLoaded(container_type values) -> void
    {
        values_ = std::make_shared<container_type>(std::move(values));
        loaded_ = true;
    }

    auto mutableValues() -> container_type&
    {
        if (values_ == nullptr)
        {
            values_ = std::make_shared<container_type>();
        }

        return *values_;
    }

    auto readableValues() const -> const container_type&
    {
        if (values_ == nullptr)
        {
            values_ = std::make_shared<container_type>();
        }

        return *values_;
    }

    mutable std::shared_ptr<container_type> values_;
    bool loaded_{};
};
} // namespace detail

template <typename T>
class OneToMany : public detail::RelationCollection<T>
{
public:
    using detail::RelationCollection<T>::RelationCollection;
};

template <typename T>
class ManyToMany : public detail::RelationCollection<T>
{
public:
    using detail::RelationCollection<T>::RelationCollection;
};

namespace detail
{
template <typename T>
struct RelationCollectionTraits
{
    inline static constexpr bool isCollection = false;
    inline static constexpr bool isOneToMany = false;
    using Target = void;
};

template <typename T>
struct RelationCollectionTraits<OneToMany<T>>
{
    inline static constexpr bool isCollection = true;
    inline static constexpr bool isOneToMany = true;
    using Target = T;
};

template <typename T>
struct RelationCollectionTraits<ManyToMany<T>>
{
    inline static constexpr bool isCollection = true;
    inline static constexpr bool isOneToMany = false;
    using Target = T;
};

template <typename T>
inline constexpr bool isRelationCollection = RelationCollectionTraits<std::remove_cv_t<T>>::isCollection;

template <typename T>
struct OptionalRelationCollectionTraits
{
    inline static constexpr bool value = false;
};

template <typename T>
struct OptionalRelationCollectionTraits<std::optional<T>>
{
    inline static constexpr bool value = isRelationCollection<T>;
};

template <typename T>
inline constexpr bool isOptionalRelationCollection = OptionalRelationCollectionTraits<std::remove_cv_t<T>>::value;
} // namespace detail

template <typename T>
inline constexpr bool is_relation_collection_v = detail::isRelationCollection<std::remove_cvref_t<T>>;

template <typename T>
inline constexpr bool is_optional_relation_collection_v = detail::isOptionalRelationCollection<std::remove_cvref_t<T>>;

template <typename T>
using relation_target_t = typename detail::RelationCollectionTraits<std::remove_cvref_t<T>>::Target;

template <typename T>
inline constexpr bool is_one_to_many_v =
    is_relation_collection_v<T> && detail::RelationCollectionTraits<std::remove_cvref_t<T>>::isOneToMany;

namespace detail
{
inline constexpr reflection::FixedString emptyRelationName{""};

template <typename>
struct RelationMemberPointerTraits;

template <typename Value, typename Owner>
struct RelationMemberPointerTraits<Value Owner::*>
{
    using OwnerType = Owner;
    using ValueType = Value;
};

template <auto Member>
using relation_member_owner_t = typename RelationMemberPointerTraits<std::remove_cv_t<decltype(Member)>>::OwnerType;

template <auto Member>
using relation_member_value_t = typename RelationMemberPointerTraits<std::remove_cv_t<decltype(Member)>>::ValueType;

template <reflection::FixedString... Names>
struct RelationColumnNames
{
    inline static constexpr std::size_t size = sizeof...(Names);
    inline static constexpr std::array<std::string_view, sizeof...(Names)> values{Names.view()...};
};

template <typename T>
struct IsRelationDescriptor : std::false_type
{
};

template <typename T>
struct IsOneToManyDescriptor : std::false_type
{
};

template <typename T>
struct IsManyToManyDescriptor : std::false_type
{
};
} // namespace detail

/**
 * @brief Typed one-to-many mapping. The descriptor stores no runtime strings:
 * every member and fallback name is encoded in its type.
 */
template <auto Member, auto MappedByMember = nullptr, reflection::FixedString MappedByName = detail::emptyRelationName>
    requires std::is_member_object_pointer_v<decltype(Member)>
struct OneToManyDescriptor
{
    inline static constexpr auto member = Member;
    inline static constexpr auto mappedByMember = MappedByMember;
    inline static constexpr auto mappedByName = MappedByName;

    template <auto TargetMember>
        requires std::is_member_object_pointer_v<decltype(TargetMember)>
    [[nodiscard]] consteval auto
    mappedBy() const -> OneToManyDescriptor<Member, TargetMember, detail::emptyRelationName>
    {
        using collection_t = std::remove_cvref_t<detail::relation_member_value_t<Member>>;
        using target_t = relation_target_t<collection_t>;
        static_assert(std::same_as<detail::relation_member_owner_t<TargetMember>, target_t>,
                      "ORM_RELATION_MAPPED_BY_OWNER: mappedBy member must belong to the relation target");
        return {};
    }

    template <reflection::FixedString TargetField>
    [[nodiscard]] consteval auto mappedBy() const -> OneToManyDescriptor<Member, nullptr, TargetField>
    {
        static_assert(not TargetField.view().empty(),
                      "ORM_RELATION_MAPPED_BY_EMPTY: mappedBy field name must not be empty");
        return {};
    }
};

namespace detail
{
template <auto Member, auto MappedByMember, reflection::FixedString MappedByName>
struct IsRelationDescriptor<OneToManyDescriptor<Member, MappedByMember, MappedByName>> : std::true_type
{
};

template <auto Member, auto MappedByMember, reflection::FixedString MappedByName>
struct IsOneToManyDescriptor<OneToManyDescriptor<Member, MappedByMember, MappedByName>> : std::true_type
{
};
} // namespace detail

/**
 * @brief Typed many-to-many mapping and its compile-time builder.
 */
template <auto Member, auto MappedByMember = nullptr, reflection::FixedString MappedByName = detail::emptyRelationName,
          reflection::FixedString ThroughTable = detail::emptyRelationName,
          typename OwnerColumnNames = detail::RelationColumnNames<>,
          typename TargetColumnNames = detail::RelationColumnNames<>>
    requires std::is_member_object_pointer_v<decltype(Member)>
struct ManyToManyDescriptor
{
    inline static constexpr auto member = Member;
    inline static constexpr auto mappedByMember = MappedByMember;
    inline static constexpr auto mappedByName = MappedByName;
    inline static constexpr auto throughTable = ThroughTable;
    using OwnerColumns = OwnerColumnNames;
    using TargetColumns = TargetColumnNames;

    template <reflection::FixedString TableName>
    [[nodiscard]] consteval auto through() const
        -> ManyToManyDescriptor<Member, MappedByMember, MappedByName, TableName, OwnerColumnNames, TargetColumnNames>
    {
        static_assert(not TableName.view().empty(),
                      "ORM_RELATION_JUNCTION_EMPTY: junction table name must not be empty");
        return {};
    }

    template <auto TargetMember>
        requires std::is_member_object_pointer_v<decltype(TargetMember)>
    [[nodiscard]] consteval auto
    mappedBy() const -> ManyToManyDescriptor<Member, TargetMember, detail::emptyRelationName, ThroughTable,
                                             OwnerColumnNames, TargetColumnNames>
    {
        using collection_t = std::remove_cvref_t<detail::relation_member_value_t<Member>>;
        using target_t = relation_target_t<collection_t>;
        static_assert(std::same_as<detail::relation_member_owner_t<TargetMember>, target_t>,
                      "ORM_RELATION_MAPPED_BY_OWNER: mappedBy member must belong to the relation target");
        return {};
    }

    template <reflection::FixedString TargetField>
    [[nodiscard]] consteval auto mappedBy() const
        -> ManyToManyDescriptor<Member, nullptr, TargetField, ThroughTable, OwnerColumnNames, TargetColumnNames>
    {
        static_assert(not TargetField.view().empty(),
                      "ORM_RELATION_MAPPED_BY_EMPTY: mappedBy field name must not be empty");
        return {};
    }

    template <reflection::FixedString... Names>
    [[nodiscard]] consteval auto
    ownerColumns() const -> ManyToManyDescriptor<Member, MappedByMember, MappedByName, ThroughTable,
                                                 detail::RelationColumnNames<Names...>, TargetColumnNames>
    {
        static_assert(sizeof...(Names) > 0,
                      "ORM_RELATION_OWNER_COLUMNS_EMPTY: owner junction columns must not be empty");
        static_assert((not Names.view().empty() && ...),
                      "ORM_RELATION_OWNER_COLUMN_EMPTY: junction column name must not be empty");
        return {};
    }

    template <reflection::FixedString... Names>
    [[nodiscard]] consteval auto
    targetColumns() const -> ManyToManyDescriptor<Member, MappedByMember, MappedByName, ThroughTable, OwnerColumnNames,
                                                  detail::RelationColumnNames<Names...>>
    {
        static_assert(sizeof...(Names) > 0,
                      "ORM_RELATION_TARGET_COLUMNS_EMPTY: target junction columns must not be empty");
        static_assert((not Names.view().empty() && ...),
                      "ORM_RELATION_TARGET_COLUMN_EMPTY: junction column name must not be empty");
        return {};
    }
};

namespace detail
{
template <auto Member, auto MappedByMember, reflection::FixedString MappedByName, reflection::FixedString ThroughTable,
          typename OwnerColumnNames, typename TargetColumnNames>
struct IsRelationDescriptor<
    ManyToManyDescriptor<Member, MappedByMember, MappedByName, ThroughTable, OwnerColumnNames, TargetColumnNames>>
    : std::true_type
{
};

template <auto Member, auto MappedByMember, reflection::FixedString MappedByName, reflection::FixedString ThroughTable,
          typename OwnerColumnNames, typename TargetColumnNames>
struct IsManyToManyDescriptor<
    ManyToManyDescriptor<Member, MappedByMember, MappedByName, ThroughTable, OwnerColumnNames, TargetColumnNames>>
    : std::true_type
{
};
} // namespace detail

template <auto Member>
    requires std::is_member_object_pointer_v<decltype(Member)>
[[nodiscard]] consteval auto oneToMany() -> OneToManyDescriptor<Member>
{
    using field_t = std::remove_cvref_t<detail::relation_member_value_t<Member>>;
    static_assert(is_one_to_many_v<field_t>, "ORM_RELATION_KIND: oneToMany<Member>() requires an OneToMany<T> member");
    return {};
}

template <auto Member>
    requires std::is_member_object_pointer_v<decltype(Member)>
[[nodiscard]] consteval auto manyToMany() -> ManyToManyDescriptor<Member>
{
    using field_t = std::remove_cvref_t<detail::relation_member_value_t<Member>>;
    static_assert(is_relation_collection_v<field_t> && not is_one_to_many_v<field_t>,
                  "ORM_RELATION_KIND: manyToMany<Member>() requires a ManyToMany<T> member");
    return {};
}

template <typename... Descriptors>
[[nodiscard]] consteval auto relations(Descriptors... descriptors)
{
    static_assert((detail::IsRelationDescriptor<std::remove_cvref_t<Descriptors>>::value && ...),
                  "ORM_MODEL_RELATIONS_DEFINITION: orm::relations accepts only typed relation descriptors");
    return std::tuple<Descriptors...>{descriptors...};
}
} // namespace orm
