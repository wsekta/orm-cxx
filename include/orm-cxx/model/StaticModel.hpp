#pragma once

#include <array>
#include <cstddef>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include "Mapping.hpp"
#include "ModelView.hpp"
#include "orm-cxx/reflection/Reflection.hpp"
#include "orm-cxx/relations.hpp"

namespace orm::model
{
template <typename... Models>
struct Schema;

namespace detail
{
template <typename>
struct StaticSchemaTraits;

template <typename... Models>
struct StaticSchemaTraits<Schema<Models...>>
{
    template <typename Model>
    inline static constexpr bool contains = (std::same_as<Model, Models> || ...);

    template <typename Model, std::size_t... Is>
    [[nodiscard]] static consteval auto indexOfImpl(std::index_sequence<Is...>) -> std::size_t
    {
        std::size_t result = 0;
        (((std::same_as<Model, Models>) ? (result = Is, true) : false) || ...);
        return result;
    }

    template <typename Model>
    [[nodiscard]] static consteval auto indexOf() -> std::size_t
    {
        static_assert(contains<Model>, "ORM_SCHEMA_MODEL_MISSING: requested model type does not belong to this schema");
        return indexOfImpl<Model>(std::make_index_sequence<sizeof...(Models)>{});
    }
};

template <typename>
struct IsFixedString : std::false_type
{
};

template <std::size_t Size>
struct IsFixedString<reflection::FixedString<Size>> : std::true_type
{
};

template <typename>
struct IsColumnNames : std::false_type
{
};

template <typename... Definitions>
struct IsColumnNames<ColumnNames<Definitions...>> : std::true_type
{
};

template <typename>
struct IsPrimaryKey : std::false_type
{
};

template <auto... Members>
struct IsPrimaryKey<PrimaryKey<Members...>> : std::true_type
{
};

template <typename>
struct IsAutoIncrement : std::false_type
{
};

template <auto... Members>
struct IsAutoIncrement<AutoIncrement<Members...>> : std::true_type
{
};

template <typename>
struct IsRelations : std::false_type
{
};

template <typename... Descriptors>
struct IsRelations<std::tuple<Descriptors...>>
    : std::bool_constant<(orm::detail::IsRelationDescriptor<Descriptors>::value && ...)>
{
};

template <typename Model>
consteval auto hasValidTableNameDefinition() -> bool
{
    if constexpr (requires { Model::table_name; })
    {
        using definition_t = std::remove_cvref_t<decltype(Model::table_name)>;
        if constexpr (IsFixedString<definition_t>::value)
        {
            return not Model::table_name.view().empty();
        }
        else
        {
            return false;
        }
    }
    else
    {
        return true;
    }
}

template <typename Model>
consteval auto hasValidColumnNamesDefinition() -> bool
{
    if constexpr (requires { Model::columns_names; })
    {
        using definition_t = std::remove_cvref_t<decltype(Model::columns_names)>;
        if constexpr (IsColumnNames<definition_t>::value)
        {
            return definition_t::template isValidFor<Model>();
        }
        else
        {
            return false;
        }
    }
    else
    {
        return true;
    }
}

template <typename Model>
consteval auto hasValidPrimaryKeyDefinition() -> bool
{
    if constexpr (requires { Model::id_columns; })
    {
        using definition_t = std::remove_cvref_t<decltype(Model::id_columns)>;
        if constexpr (IsPrimaryKey<definition_t>::value)
        {
            return definition_t::template isValidFor<Model>();
        }
        else
        {
            return false;
        }
    }
    else
    {
        return true;
    }
}

template <typename Model>
consteval auto hasValidAutoIncrementDefinition() -> bool
{
    if constexpr (requires { Model::auto_increment_columns; })
    {
        using definition_t = std::remove_cvref_t<decltype(Model::auto_increment_columns)>;
        if constexpr (IsAutoIncrement<definition_t>::value)
        {
            return definition_t::template isValidFor<Model>();
        }
        else
        {
            return false;
        }
    }
    else
    {
        return true;
    }
}

template <typename Model>
consteval auto normalizedDefaultTableName()
{
    constexpr auto source = reflection::typeNameStorage<Model>.view();
    constexpr auto prefixSize = source.starts_with("class ")  ? std::string_view{"class "}.size() :
                                source.starts_with("struct ") ? std::string_view{"struct "}.size() :
                                                                0U;
    constexpr auto resultSize = [source, prefixSize]
    {
        std::size_t result{};
        for (std::size_t index = prefixSize; index < source.size(); ++index)
        {
            ++result;
            if (source[index] == ':' && index + 1 < source.size() && source[index + 1] == ':')
            {
                ++index;
            }
        }
        return result;
    }();

    std::array<char, resultSize + 1> result{};
    std::size_t output{};
    for (std::size_t index = prefixSize; index < source.size(); ++index)
    {
        if (source[index] == ':' && index + 1 < source.size() && source[index + 1] == ':')
        {
            result[output++] = '_';
            ++index;
        }
        else
        {
            result[output++] = source[index];
        }
    }
    return result;
}

template <typename Model>
inline constexpr auto defaultTableNameStorage = normalizedDefaultTableName<Model>();

template <typename Model>
consteval auto modelTypeName() -> std::string_view
{
    return reflection::typeNameStorage<Model>.view();
}

template <typename Model>
consteval auto mappedTableName() -> std::string_view
{
    if constexpr (requires { Model::table_name; })
    {
        using definition_t = std::remove_cvref_t<decltype(Model::table_name)>;
        if constexpr (IsFixedString<definition_t>::value)
        {
            return Model::table_name.view();
        }
    }
    constexpr auto& storage = defaultTableNameStorage<Model>;
    return std::string_view{storage.data(), storage.size() - 1};
}

template <typename Model>
consteval auto mappedColumnName(std::string_view fieldName) -> std::string_view
{
    if constexpr (requires { Model::columns_names; })
    {
        using definition_t = std::remove_cvref_t<decltype(Model::columns_names)>;
        if constexpr (IsColumnNames<definition_t>::value && definition_t::template isValidFor<Model>())
        {
            if (const auto mapped = Model::columns_names.find(fieldName); mapped.has_value())
            {
                return mapped.value();
            }
        }
    }
    return fieldName;
}

template <typename Model>
consteval auto isPrimaryKey(std::string_view fieldName) -> bool
{
    if constexpr (requires { Model::id_columns; })
    {
        using definition_t = std::remove_cvref_t<decltype(Model::id_columns)>;
        if constexpr (IsPrimaryKey<definition_t>::value && definition_t::template isValidFor<Model>())
        {
            return Model::id_columns.contains(fieldName);
        }
        return false;
    }
    else
    {
        return fieldName == "id";
    }
}

template <typename Model>
consteval auto isAutoIncrement(std::string_view fieldName) -> bool
{
    if constexpr (requires { Model::auto_increment_columns; })
    {
        using definition_t = std::remove_cvref_t<decltype(Model::auto_increment_columns)>;
        if constexpr (IsAutoIncrement<definition_t>::value && definition_t::template isValidFor<Model>())
        {
            return Model::auto_increment_columns.contains(fieldName);
        }
        return false;
    }
    else
    {
        return false;
    }
}

template <typename Model, std::size_t... Indices>
consteval auto hasDefaultPrimaryKey(std::index_sequence<Indices...>) -> bool
{
    return (((reflectedFieldName<Model, Indices>() == "id") &&
             not is_relation_collection_v<reflection::field_type_t<Model, Indices>> &&
             not is_optional_relation_collection_v<reflection::field_type_t<Model, Indices>>) ||
            ...);
}

template <typename Model>
consteval auto hasPrimaryKey() -> bool
{
    if constexpr (requires { Model::id_columns; })
    {
        using definition_t = std::remove_cvref_t<decltype(Model::id_columns)>;
        if constexpr (IsPrimaryKey<definition_t>::value)
        {
            return definition_t::size != 0 && definition_t::template isValidFor<Model>();
        }
        else
        {
            return false;
        }
    }
    else
    {
        return hasDefaultPrimaryKey<Model>(std::make_index_sequence<reflection::fieldCount<Model>>{});
    }
}

template <typename Model, typename SchemaType, std::size_t Index>
consteval auto makeColumn() -> ColumnView
{
    using field_t = reflection::field_type_t<Model, Index>;
    using value_t = std::remove_cv_t<static_optional_value_t<std::remove_cvref_t<field_t>>>;
    constexpr auto fieldName = reflectedFieldName<Model, Index>();
    static_assert(not is_optional_relation_collection_v<field_t>,
                  "ORM_MODEL_OPTIONAL_COLLECTION: relation collections cannot be optional");

    if constexpr (StaticSchemaTraits<SchemaType>::template contains<value_t>)
    {
        static_assert(hasPrimaryKey<value_t>(),
                      "ORM_RELATION_KEYLESS: every to-one target must define a valid primary key");
        return ColumnView{
            .fieldIndex = Index,
            .fieldName = fieldName,
            .name = mappedColumnName<Model>(fieldName),
            .type = std::nullopt,
            .kind = FieldKind::ToOne,
            .targetModelIndex = StaticSchemaTraits<SchemaType>::template indexOf<value_t>(),
            .isPrimaryKey = isPrimaryKey<Model>(fieldName),
            .isAutoIncrement = isAutoIncrement<Model>(fieldName),
            .isNotNull = not isNullable<field_t>,
        };
    }
    else
    {
        return ColumnView{
            .fieldIndex = Index,
            .fieldName = fieldName,
            .name = mappedColumnName<Model>(fieldName),
            .type = logicalType<field_t>(),
            .kind = FieldKind::Scalar,
            .targetModelIndex = noTargetModel,
            .isPrimaryKey = isPrimaryKey<Model>(fieldName),
            .isAutoIncrement = isAutoIncrement<Model>(fieldName),
            .isNotNull = not isNullable<field_t>,
        };
    }
}

template <typename Model, std::size_t OutputIndex, std::size_t... FieldIndices>
consteval auto columnFieldIndex(std::index_sequence<FieldIndices...>) -> std::size_t
{
    constexpr std::array fieldIndices{FieldIndices...};
    constexpr std::array included{
        (not is_relation_collection_v<reflection::field_type_t<Model, FieldIndices>> &&
         not is_optional_relation_collection_v<reflection::field_type_t<Model, FieldIndices>>)...};
    std::size_t output{};
    std::size_t result = reflection::fieldCount<Model>;
    for (std::size_t index = 0; index < included.size(); ++index)
    {
        if (included[index])
        {
            if (output == OutputIndex)
            {
                result = fieldIndices[index];
                break;
            }
            ++output;
        }
    }
    return result;
}

template <typename Model, typename SchemaType, std::size_t... FieldIndices, std::size_t... OutputIndices>
consteval auto makeColumnsDirect(std::index_sequence<FieldIndices...>, std::index_sequence<OutputIndices...>)
{
    return std::array<ColumnView, sizeof...(OutputIndices)>{
        makeColumn<Model, SchemaType,
                   columnFieldIndex<Model, OutputIndices>(std::index_sequence<FieldIndices...>{})>()...};
}

template <typename Model, typename SchemaType, std::size_t... Indices>
consteval auto makeColumns(std::index_sequence<Indices...> fields)
{
    static_assert((not is_optional_relation_collection_v<reflection::field_type_t<Model, Indices>> && ...),
                  "ORM_MODEL_OPTIONAL_COLLECTION: relation collections cannot be optional");
    constexpr auto columnCount =
        ((not is_relation_collection_v<reflection::field_type_t<Model, Indices>> ? 1U : 0U) + ... + 0U);
    return makeColumnsDirect<Model, SchemaType>(fields, std::make_index_sequence<columnCount>{});
}

template <std::size_t Size>
consteval auto hasValidColumnNames(const std::array<ColumnView, Size>& columns) -> bool
{
    bool valid = true;
    for (std::size_t left = 0; left < columns.size() && valid; ++left)
    {
        if (columns[left].fieldName.empty() || columns[left].name.empty())
        {
            valid = false;
            break;
        }
        for (std::size_t right = left + 1; right < columns.size() && valid; ++right)
        {
            if (columns[left].fieldName == columns[right].fieldName || columns[left].name == columns[right].name ||
                columns[left].fieldName == columns[right].name || columns[left].name == columns[right].fieldName)
            {
                valid = false;
                break;
            }
        }
    }
    return valid;
}

template <std::size_t Size>
consteval auto hasValidPrimaryKey(const std::array<ColumnView, Size>& columns) -> bool
{
    bool valid = true;
    for (const auto& column : columns)
    {
        if (column.isPrimaryKey && (not column.isNotNull || column.kind != FieldKind::Scalar))
        {
            valid = false;
            break;
        }
    }
    return valid;
}

template <std::size_t Size>
consteval auto hasValidAutoIncrement(const std::array<ColumnView, Size>& columns) -> bool
{
    std::size_t primaryKeyCount{};
    std::size_t autoIncrementCount{};
    const ColumnView* autoIncrementColumn{};

    for (const auto& column : columns)
    {
        primaryKeyCount += column.isPrimaryKey ? 1U : 0U;
        if (column.isAutoIncrement)
        {
            ++autoIncrementCount;
            autoIncrementColumn = &column;
        }
    }

    if (autoIncrementCount == 0)
    {
        return true;
    }
    return autoIncrementCount == 1 && primaryKeyCount == 1 && autoIncrementColumn->isPrimaryKey &&
           autoIncrementColumn->isNotNull && autoIncrementColumn->kind == FieldKind::Scalar &&
           autoIncrementColumn->type == ColumnType::Int;
}

template <typename Model>
consteval auto modelPrimaryKeyCount() -> std::size_t
{
    if constexpr (requires { Model::id_columns; })
    {
        using definition_t = std::remove_cvref_t<decltype(Model::id_columns)>;
        if constexpr (IsPrimaryKey<definition_t>::value)
        {
            return definition_t::size;
        }
        else
        {
            return 0;
        }
    }
    else
    {
        return hasDefaultPrimaryKey<Model>(std::make_index_sequence<reflection::fieldCount<Model>>{}) ? 1U : 0U;
    }
}

template <std::size_t ResultSize, std::size_t ColumnCount>
consteval auto makePrimaryKeyIndices(const std::array<ColumnView, ColumnCount>& columns)
{
    std::array<std::size_t, ResultSize> result{};
    std::size_t output{};
    for (std::size_t index = 0; index < columns.size(); ++index)
    {
        if (columns[index].isPrimaryKey)
        {
            result[output++] = index;
        }
    }
    return result;
}

template <typename Model, std::size_t ColumnCount>
consteval auto makeModelPrimaryKeyIndices(const std::array<ColumnView, ColumnCount>& columns)
{
    if constexpr (requires { Model::id_columns; })
    {
        using definition_t = std::remove_cvref_t<decltype(Model::id_columns)>;
        if constexpr (IsPrimaryKey<definition_t>::value && definition_t::template isValidFor<Model>())
        {
            constexpr auto fieldIndices = definition_t::template indices<Model>();
            std::array<std::size_t, fieldIndices.size()> result{};
            for (std::size_t keyIndex = 0; keyIndex < fieldIndices.size(); ++keyIndex)
            {
                bool found{};
                for (std::size_t columnIndex = 0; columnIndex < columns.size(); ++columnIndex)
                {
                    if (columns[columnIndex].fieldIndex == fieldIndices[keyIndex])
                    {
                        result[keyIndex] = columnIndex;
                        found = true;
                        break;
                    }
                }
                if (not found)
                {
                    result[keyIndex] = columns.size();
                }
            }
            return result;
        }
        else
        {
            return std::array<std::size_t, 0>{};
        }
    }
    else
    {
        return makePrimaryKeyIndices<modelPrimaryKeyCount<Model>()>(columns);
    }
}

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
                if (names[left] == names[right])
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

template <typename Model, typename SchemaType>
inline constexpr auto staticColumns =
    makeColumns<Model, SchemaType>(std::make_index_sequence<reflection::fieldCount<Model>>{});

template <typename Model, typename SchemaType>
inline constexpr auto staticPrimaryKeyIndices = makeModelPrimaryKeyIndices<Model>(staticColumns<Model, SchemaType>);

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

template <typename... Models>
consteval auto hasUniqueTypes() -> bool
{
    constexpr std::array<TypeId, sizeof...(Models)> ids{typeId<Models>()...};
    bool unique = true;
    for (std::size_t left = 0; left < ids.size() && unique; ++left)
    {
        for (std::size_t right = left + 1; right < ids.size() && unique; ++right)
        {
            if (ids[left] == ids[right])
            {
                unique = false;
            }
        }
    }
    return unique;
}
} // namespace detail

/**
 * @brief Materializes one aggregate model's complete scalar metadata in static
 * storage.
 */
template <typename T, typename SchemaType>
struct StaticModel
{
    static_assert(detail::StaticSchemaTraits<SchemaType>::template contains<T>,
                  "ORM_SCHEMA_MODEL_MISSING: a static descriptor requires its model to belong to Schema");
    static_assert(std::is_aggregate_v<T>, "ORM_MODEL_AGGREGATE: an ORM model must be an aggregate");
    static_assert(reflection::fieldCount<T> <= 128,
                  "ORM_MODEL_FIELD_LIMIT: automatic model reflection supports at most 128 fields");
    static_assert(detail::hasValidTableNameDefinition<T>(),
                  "ORM_MODEL_TABLE_NAME: table_name must be a non-empty orm::reflection::FixedString");
    static_assert(detail::hasValidColumnNamesDefinition<T>(),
                  "ORM_MODEL_COLUMN_MAPPING: columns_names must be a valid typed columnNames(...) definition");
    static_assert(
        detail::hasValidPrimaryKeyDefinition<T>(),
        "ORM_MODEL_PRIMARY_KEY_DEFINITION: id_columns must be a valid typed primaryKey<Members...>() definition");
    static_assert(detail::hasValidAutoIncrementDefinition<T>(),
                  "ORM_MODEL_AUTO_INCREMENT_DEFINITION: auto_increment_columns must be a valid typed "
                  "autoIncrement<Members...>() definition");
    static_assert(detail::hasValidRelationsDefinition<T>(),
                  "ORM_MODEL_RELATIONS_DEFINITION: relations must be a typed orm::relations(...) definition");
    static_assert(detail::hasCompleteRelationDefinitions<T>(),
                  "ORM_RELATION_DESCRIPTOR_COUNT: every collection requires exactly one matching descriptor");

    inline static constexpr auto columns = detail::staticColumns<T, SchemaType>;

    static_assert(detail::hasValidColumnNames(columns),
                  "ORM_MODEL_COLUMN_NAME: model field and SQL column names must be non-empty and unique");
    static_assert(detail::hasValidPrimaryKey(columns),
                  "ORM_MODEL_PRIMARY_KEY: primary-key members must be non-null scalar fields");
    static_assert(detail::hasValidAutoIncrement(columns), "ORM_MODEL_AUTO_INCREMENT: auto increment requires one "
                                                          "non-null int member which is the model's only primary key");

    inline static constexpr auto primaryKeyIndices = detail::staticPrimaryKeyIndices<T, SchemaType>;
    static_assert(
        []
        {
            for (const auto index : primaryKeyIndices)
            {
                if (index >= columns.size())
                {
                    return false;
                }
            }
            return true;
        }(),
        "ORM_MODEL_PRIMARY_KEY_RELATION: primary keys must refer to scalar columns");

    inline static constexpr auto relations = detail::makeRelations<T, SchemaType>();

    inline static constexpr ModelDataView data{
        .type = typeId<T>(),
        .schemaIndex = detail::StaticSchemaTraits<SchemaType>::template indexOf<T>(),
        .typeName = detail::modelTypeName<T>(),
        .tableName = detail::mappedTableName<T>(),
        .columns = columns,
        .primaryKeyIndices = primaryKeyIndices,
        .relations = relations,
    };
};

template <typename SchemaType, typename T>
[[nodiscard]] consteval auto modelDescriptor() noexcept -> StaticModel<T, SchemaType>
{
    return {};
}

template <typename SchemaType, typename T>
[[nodiscard]] constexpr auto modelView() noexcept -> ModelView
{
    return ModelView{&SchemaType::view, detail::StaticSchemaTraits<SchemaType>::template indexOf<T>()};
}

template <typename T>
[[nodiscard]] consteval auto tableName() -> std::string_view
{
    return detail::mappedTableName<T>();
}
} // namespace orm::model
