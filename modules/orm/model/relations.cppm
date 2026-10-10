module;

#include <array>
#include <concepts>
#include <cstddef>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

export module orm:model_relations;

import orm.reflection;
import :foundation;
import :model_mapping;
import :model_views;
import :model_columns;

namespace orm::model::detail
{
template <typename Model>
consteval auto relationDefinitions()
{
    if constexpr (requires { Model::relations; })
    {
        using definition_t = std::remove_cvref_t<decltype(Model::relations)>;
        if constexpr (IsRelations<definition_t>::value)
        {
            return Model::relations;
        }
        else
        {
            return std::tuple{};
        }
    }
    else
    {
        return std::tuple{};
    }
}

template <typename Model>
using relation_definitions_t = std::remove_cvref_t<decltype(relationDefinitions<Model>())>;

template <typename Model>
consteval auto hasValidRelationsDefinition() -> bool
{
    if constexpr (requires { Model::relations; })
    {
        using definition_t = std::remove_cvref_t<decltype(Model::relations)>;
        return IsRelations<definition_t>::value;
    }
    return true;
}

template <typename Descriptor>
consteval auto relationFieldName() -> std::string_view
{
    return reflectedMemberNameStorage<Descriptor::member>.view();
}

template <typename Descriptor>
consteval auto relationMappedByName() -> std::string_view
{
    if constexpr (std::is_same_v<std::remove_cv_t<decltype(Descriptor::mappedByMember)>, std::nullptr_t>)
    {
        return Descriptor::mappedByName.view();
    }
    else
    {
        return reflectedMemberNameStorage<Descriptor::mappedByMember>.view();
    }
}

template <typename Descriptor>
using relation_field_t = std::remove_cvref_t<orm::detail::relation_member_value_t<Descriptor::member>>;

template <typename Descriptor>
using relation_target_t = orm::relation_target_t<relation_field_t<Descriptor>>;

template <typename Model, typename Descriptor>
consteval auto isValidRelationDescriptor() -> bool
{
    if constexpr (not orm::detail::IsRelationDescriptor<Descriptor>::value)
    {
        return false;
    }
    else if constexpr (not std::same_as<orm::detail::relation_member_owner_t<Descriptor::member>, Model>)
    {
        return false;
    }
    else if constexpr (not memberExists<Model, Descriptor::member>())
    {
        return false;
    }
    else if constexpr (orm::detail::IsOneToManyDescriptor<Descriptor>::value)
    {
        return is_one_to_many_v<relation_field_t<Descriptor>>;
    }
    else
    {
        return is_relation_collection_v<relation_field_t<Descriptor>> &&
               not is_one_to_many_v<relation_field_t<Descriptor>>;
    }
}

template <typename Model, typename... Descriptors>
consteval auto hasUniqueValidRelationDescriptors(std::tuple<Descriptors...>) -> bool
{
    if constexpr (not(isValidRelationDescriptor<Model, Descriptors>() && ...))
    {
        return false;
    }
    else
    {
        constexpr std::array<std::string_view, sizeof...(Descriptors)> names{relationFieldName<Descriptors>()...};
        bool unique = true;
        for (std::size_t left = 0; left < names.size() && unique; ++left)
        {
            for (std::size_t right = left + 1; right < names.size() && unique; ++right)
            {
                if (names[left].compare(names[right]) == 0)
                {
                    unique = false;
                }
            }
        }
        return unique;
    }
}

template <typename Model, std::size_t... Indices>
consteval auto collectionFieldCount(std::index_sequence<Indices...>) -> std::size_t
{
    return ((is_relation_collection_v<reflection::field_type_t<Model, Indices>> ? 1U : 0U) + ... + 0U);
}

template <typename Model>
consteval auto hasCompleteRelationDefinitions() -> bool
{
    constexpr auto definitions = relationDefinitions<Model>();
    return hasValidRelationsDefinition<Model>() && hasUniqueValidRelationDescriptors<Model>(definitions) &&
           std::tuple_size_v<relation_definitions_t<Model>> ==
               collectionFieldCount<Model>(std::make_index_sequence<reflection::fieldCount<Model>>{});
}

template <typename Model>
consteval auto validatedRelationDefinitions()
{
    if constexpr (hasCompleteRelationDefinitions<Model>())
    {
        return relationDefinitions<Model>();
    }
    else
    {
        return std::tuple{};
    }
}

template <std::size_t Capacity>
struct RelationNameBuffer
{
    std::array<char, Capacity> data{};
    std::size_t size{};

    [[nodiscard]] constexpr auto view() const noexcept -> std::string_view
    {
        return {data.data(), size};
    }
};

template <typename Model, typename SchemaType>
struct DefaultJunctionColumnStorage
{
    inline static constexpr auto& columns = staticColumns<Model, SchemaType>;
    inline static constexpr auto& primaryKeyIndices = staticPrimaryKeyIndices<Model, SchemaType>;
    inline static constexpr auto capacity = []
    {
        std::size_t result = mappedTableName<Model>().size() + 2;
        for (const auto columnIndex : primaryKeyIndices)
        {
            result = result < mappedTableName<Model>().size() + 1 + columns[columnIndex].name.size() + 1 ?
                         mappedTableName<Model>().size() + 1 + columns[columnIndex].name.size() + 1 :
                         result;
        }
        return result;
    }();

    inline static constexpr auto buffers = []
    {
        std::array<RelationNameBuffer<capacity>, primaryKeyIndices.size()> result{};
        for (std::size_t keyIndex = 0; keyIndex < primaryKeyIndices.size(); ++keyIndex)
        {
            auto& output = result[keyIndex];
            const auto table = mappedTableName<Model>();
            const auto column = columns[primaryKeyIndices[keyIndex]].name;
            for (const auto character : table)
            {
                output.data[output.size++] = character;
            }
            output.data[output.size++] = '_';
            for (const auto character : column)
            {
                output.data[output.size++] = character;
            }
        }
        return result;
    }();

    inline static constexpr auto names = []
    {
        std::array<std::string_view, primaryKeyIndices.size()> result{};
        for (std::size_t index = 0; index < result.size(); ++index)
        {
            result[index] = buffers[index].view();
        }
        return result;
    }();
};

template <typename Descriptor, typename Owner, typename Target, typename SchemaType>
struct JunctionStorage
{
    inline static constexpr auto& defaultOwner = DefaultJunctionColumnStorage<Owner, SchemaType>::names;
    inline static constexpr auto& defaultTarget = DefaultJunctionColumnStorage<Target, SchemaType>::names;
    inline static constexpr auto& owner = []() -> const auto&
    {
        if constexpr (Descriptor::OwnerColumns::size == 0)
        {
            return defaultOwner;
        }
        else
        {
            return Descriptor::OwnerColumns::values;
        }
    }();
    inline static constexpr auto& target = []() -> const auto&
    {
        if constexpr (Descriptor::TargetColumns::size == 0)
        {
            return defaultTarget;
        }
        else
        {
            return Descriptor::TargetColumns::values;
        }
    }();
};

template <typename Model, typename SchemaType, std::size_t Index>
consteval auto isToOneField() -> bool
{
    using field_t = reflection::field_type_t<Model, Index>;
    using value_t = std::remove_cv_t<static_optional_value_t<std::remove_cvref_t<field_t>>>;
    return not is_relation_collection_v<field_t> && StaticSchemaTraits<SchemaType>::template contains<value_t>;
}

template <typename Model, typename SchemaType, std::size_t... Indices>
consteval auto makeToOneRelations(std::index_sequence<Indices...>)
{
    constexpr auto count = ((isToOneField<Model, SchemaType, Indices>() ? 1U : 0U) + ... + 0U);
    std::array<RelationView, count> result{};
    std::size_t output{};
    (
        [&]
        {
            if constexpr (isToOneField<Model, SchemaType, Indices>())
            {
                using field_t = reflection::field_type_t<Model, Indices>;
                using target_t = std::remove_cv_t<static_optional_value_t<std::remove_cvref_t<field_t>>>;
                constexpr auto fieldName = reflectedFieldName<Model, Indices>();
                result[output++] = RelationView{
                    .fieldIndex = Indices,
                    .fieldName = fieldName,
                    .columnName = mappedColumnName<Model>(fieldName),
                    .kind = RelationKind::ToOne,
                    .mappedBy = {},
                    .nullable = isNullable<field_t>,
                    .targetModelIndex = StaticSchemaTraits<SchemaType>::template indexOf<target_t>(),
                    .junction = {},
                };
            }
        }(),
        ...);
    return result;
}

template <typename Model, typename SchemaType, typename Descriptor>
consteval auto makeCollectionRelation() -> RelationView
{
    using target_t = relation_target_t<Descriptor>;
    if constexpr (not StaticSchemaTraits<SchemaType>::template contains<target_t>)
    {
        static_assert(StaticSchemaTraits<SchemaType>::template contains<target_t>,
                      "ORM_RELATION_TARGET_MISSING: collection target must belong to Schema");
        return {};
    }
    else
    {
        static_assert(hasPrimaryKey<Model>() && hasPrimaryKey<target_t>(),
                      "ORM_RELATION_KEYLESS: collection endpoints must define primary keys");

        constexpr auto fieldName = relationFieldName<Descriptor>();
        constexpr auto mappedBy = relationMappedByName<Descriptor>();
        RelationView result{
            .fieldIndex = memberIndex<Model, Descriptor::member>(),
            .fieldName = fieldName,
            .columnName = fieldName,
            .kind = orm::detail::IsOneToManyDescriptor<Descriptor>::value ? RelationKind::OneToMany :
                                                                            RelationKind::ManyToMany,
            .mappedBy = mappedBy,
            .nullable = false,
            .targetModelIndex = StaticSchemaTraits<SchemaType>::template indexOf<target_t>(),
            .junction = {},
        };

        if constexpr (orm::detail::IsOneToManyDescriptor<Descriptor>::value)
        {
            static_assert(not mappedBy.empty(), "ORM_RELATION_ONE_TO_MANY_MAPPED_BY: one-to-many requires mappedBy");
        }
        else
        {
            constexpr auto isInverse = not mappedBy.empty();
            if constexpr (isInverse)
            {
                static_assert(Descriptor::throughTable.view().empty() && Descriptor::OwnerColumns::size == 0 &&
                                  Descriptor::TargetColumns::size == 0,
                              "ORM_RELATION_INVERSE_CONFIG: inverse many-to-many may only use mappedBy");
            }
            else
            {
                static_assert(not Descriptor::throughTable.view().empty(),
                              "ORM_RELATION_JUNCTION_MISSING: owning many-to-many requires through");
                using storage_t = JunctionStorage<Descriptor, Model, target_t, SchemaType>;
                static_assert(storage_t::owner.size() == staticPrimaryKeyIndices<Model, SchemaType>.size(),
                              "ORM_RELATION_OWNER_COLUMN_COUNT: owner junction columns must match owner PK");
                static_assert(storage_t::target.size() == staticPrimaryKeyIndices<target_t, SchemaType>.size(),
                              "ORM_RELATION_TARGET_COLUMN_COUNT: target junction columns must match target PK");
                result.junction = JunctionView{
                    .tableName = Descriptor::throughTable.view(),
                    .ownerColumns = storage_t::owner,
                    .targetColumns = storage_t::target,
                    .owningSide = true,
                };
            }
        }
        return result;
    }
}

template <typename Model, typename SchemaType, typename... Descriptors>
consteval auto makeCollectionRelations(std::tuple<Descriptors...>)
{
    return std::array<RelationView, sizeof...(Descriptors)>{
        makeCollectionRelation<Model, SchemaType, Descriptors>()...};
}

template <typename Model, typename SchemaType>
consteval auto makeRelations()
{
    constexpr auto toOne =
        makeToOneRelations<Model, SchemaType>(std::make_index_sequence<reflection::fieldCount<Model>>{});
    constexpr auto collections = makeCollectionRelations<Model, SchemaType>(validatedRelationDefinitions<Model>());
    std::array<RelationView, toOne.size() + collections.size()> result{};
    std::size_t output{};
    for (const auto& relation : toOne)
    {
        result[output++] = relation;
    }
    for (const auto& relation : collections)
    {
        result[output++] = relation;
    }
    return result;
}
} // namespace orm::model::detail
