#pragma once

#include <algorithm>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "IdInfo.hpp"
#include "ModelInfo.hpp"
#include "NameMapping.hpp"
#include "TableInfo.hpp"
#include "orm-cxx/relations.hpp"
#include "orm-cxx/utils/ConstexprFor.hpp"
#include "orm-cxx/utils/DisableExternalsWarning.hpp"

DISABLE_WARNING_PUSH

DISABLE_EXTERNAL_WARNINGS

#include <rfl.hpp>

DISABLE_WARNING_POP

namespace orm::model
{
template <typename T>
auto getCachedModelInfo(bool force = false) -> ModelInfo&;

namespace detail
{
template <typename T>
struct OptionalTraits
{
    inline static constexpr bool isOptional = false;
    using Value = T;
};

template <typename T>
struct OptionalTraits<std::optional<T>>
{
    inline static constexpr bool isOptional = true;
    using Value = T;
};

template <typename T>
using optional_value_t = typename OptionalTraits<std::remove_cv_t<T>>::Value;

template <typename T>
auto orderedPrimaryKeyColumns() -> std::vector<std::string>
{
    const auto ids = getPrimaryIdColumnsNames<T>();
    auto fields = rfl::fields<T>();
    using model_tuple_t = std::decay_t<decltype(rfl::to_view(std::declval<T&>()).values())>;
    std::vector<std::string> result;
    result.reserve(ids.size());

    auto appendPrimaryKey = [&ids, &fields, &result](auto i, auto fieldPointer)
    {
        using field_t = std::decay_t<decltype(*fieldPointer)>;
        if constexpr (is_relation_collection_v<field_t> || is_optional_relation_collection_v<field_t>)
        {
            return;
        }
        else
        {
            auto name = getColumnName<T>(fields[i].name());
            if (ids.contains(name))
            {
                result.push_back(std::move(name));
            }
        }
    };
    utils::constexpr_for_tuple<model_tuple_t>(appendPrimaryKey);

    return result;
}

template <typename T>
auto requirePrimaryKey(std::string_view relationField) -> std::vector<std::string>
{
    const auto ids = getPrimaryIdColumnsNames<T>();
    const auto orderedIds = orderedPrimaryKeyColumns<T>();
    if (ids.empty() || orderedIds.size() != ids.size())
    {
        throw std::invalid_argument{
            "Relation '" + std::string{relationField} + "' requires a model with a valid, non-empty primary key"};
    }
    return orderedIds;
}

template <typename T>
auto defaultJunctionColumns(const std::vector<std::string>& primaryKeyColumns) -> std::vector<std::string>
{
    std::vector<std::string> result;
    result.reserve(primaryKeyColumns.size());
    for (const auto& column : primaryKeyColumns)
    {
        result.emplace_back(std::string{getTableName<T>()} + "_" + column);
    }
    return result;
}

inline auto validateJunctionColumnNames(
    const std::vector<std::string>& columns, std::string_view relationField, std::string_view side) -> void
{
    std::unordered_set<std::string> uniqueNames;
    for (const auto& column : columns)
    {
        if (column.empty() || not uniqueNames.insert(column).second)
        {
            throw std::invalid_argument{"Relation '" + std::string{relationField} + "' has invalid " +
                                        std::string{side} + " junction columns"};
        }
    }
}

template <typename Owner, typename Target>
auto makeOwningJunction(const ManyToManyDescriptor& descriptor) -> JunctionInfo
{
    if (descriptor.throughTable().empty() || not descriptor.mappedByField().empty())
    {
        throw std::invalid_argument{"Owning many-to-many relation '" + descriptor.fieldName() +
                                    "' requires through() and cannot use mappedBy()"};
    }

    if (descriptor.throughTable() == getTableName<Owner>() || descriptor.throughTable() == getTableName<Target>())
    {
        throw std::invalid_argument{"Junction table for relation '" + descriptor.fieldName() +
                                    "' conflicts with an endpoint table"};
    }

    const auto ownerIds = requirePrimaryKey<Owner>(descriptor.fieldName());
    const auto targetIds = requirePrimaryKey<Target>(descriptor.fieldName());

    if (not descriptor.ownerColumnNames().empty() && descriptor.ownerColumnNames().size() != ownerIds.size())
    {
        throw std::invalid_argument{"Relation '" + descriptor.fieldName() +
                                    "' has an ownerColumns() count different from the owner primary key"};
    }
    if (not descriptor.targetColumnNames().empty() && descriptor.targetColumnNames().size() != targetIds.size())
    {
        throw std::invalid_argument{"Relation '" + descriptor.fieldName() +
                                    "' has a targetColumns() count different from the target primary key"};
    }

    if constexpr (std::is_same_v<Owner, Target>)
    {
        if (descriptor.ownerColumnNames().empty() || descriptor.targetColumnNames().empty())
        {
            throw std::invalid_argument{"Self-referencing many-to-many relation '" + descriptor.fieldName() +
                                        "' requires explicit ownerColumns() and targetColumns()"};
        }
    }

    auto ownerColumns = descriptor.ownerColumnNames().empty() ? defaultJunctionColumns<Owner>(ownerIds)
                                                               : descriptor.ownerColumnNames();
    auto targetColumns = descriptor.targetColumnNames().empty() ? defaultJunctionColumns<Target>(targetIds)
                                                                 : descriptor.targetColumnNames();

    validateJunctionColumnNames(ownerColumns, descriptor.fieldName(), "owner");
    validateJunctionColumnNames(targetColumns, descriptor.fieldName(), "target");

    for (const auto& ownerColumn : ownerColumns)
    {
        if (std::ranges::find(targetColumns, ownerColumn) != targetColumns.end())
        {
            throw std::invalid_argument{"Relation '" + descriptor.fieldName() +
                                        "' produces colliding junction column names"};
        }
    }

    return JunctionInfo{
        descriptor.throughTable(), std::move(ownerColumns), std::move(targetColumns), true};
}

template <typename Model, typename Callback>
auto visitField(std::string_view fieldName, Callback&& callback) -> bool
{
    auto fields = rfl::fields<Model>();
    using model_tuple_t = std::decay_t<decltype(rfl::to_view(std::declval<Model&>()).values())>;
    bool found = false;

    auto visitor = [&fields, fieldName, &callback, &found](auto i, auto fieldPointer)
    {
        if (fields[i].name() == fieldName)
        {
            using field_t = std::decay_t<decltype(*fieldPointer)>;
            found = true;
            callback.template operator()<field_t>();
        }
    };
    utils::constexpr_for_tuple<model_tuple_t>(visitor);
    return found;
}

template <typename Owner, typename Target>
auto validateOneToManyMappedBy(const OneToManyDescriptor& descriptor) -> bool
{
    if (descriptor.mappedByField().empty())
    {
        throw std::invalid_argument{"One-to-many relation '" + descriptor.fieldName() + "' requires mappedBy()"};
    }

    bool compatible = false;
    bool nullable = false;
    const auto found = visitField<Target>(descriptor.mappedByField(), [&]<typename Field>()
                                         {
                                             using field_t = std::remove_cv_t<Field>;
                                             using value_t = optional_value_t<field_t>;
                                             compatible = std::is_same_v<value_t, Owner>;
                                             nullable = OptionalTraits<field_t>::isOptional;
                                         });
    if (not found || not compatible)
    {
        throw std::invalid_argument{"One-to-many relation '" + descriptor.fieldName() +
                                    "' mappedBy() must name a compatible to-one field on the target model"};
    }
    return nullable;
}

template <typename Current, typename Target>
auto makeInverseJunction(const ManyToManyDescriptor& inverseDescriptor) -> JunctionInfo
{
    if (inverseDescriptor.mappedByField().empty() || not inverseDescriptor.throughTable().empty() ||
        not inverseDescriptor.ownerColumnNames().empty() || not inverseDescriptor.targetColumnNames().empty())
    {
        throw std::invalid_argument{"Inverse many-to-many relation '" + inverseDescriptor.fieldName() +
                                    "' must use only mappedBy()"};
    }

    bool compatibleField = false;
    const auto foundField = visitField<Target>(inverseDescriptor.mappedByField(), [&]<typename Field>()
                                               {
                                                   compatibleField =
                                                       std::is_same_v<std::remove_cv_t<Field>, ManyToMany<Current>>;
                                               });
    if (not foundField || not compatibleField)
    {
        throw std::invalid_argument{"Inverse many-to-many relation '" + inverseDescriptor.fieldName() +
                                    "' mappedBy() must name a compatible many-to-many field"};
    }

    const ManyToManyDescriptor* owningDescriptor = nullptr;
    std::size_t descriptorCount = 0;
    if constexpr (requires { Target::relations; })
    {
        utils::constexpr_for_tuple(Target::relations, [&](auto, const auto& descriptor)
                                   {
                                       if (descriptor.fieldName() == inverseDescriptor.mappedByField())
                                       {
                                           ++descriptorCount;
                                           using descriptor_t = std::decay_t<decltype(descriptor)>;
                                           if constexpr (std::is_same_v<descriptor_t, ManyToManyDescriptor>)
                                           {
                                               owningDescriptor = &descriptor;
                                           }
                                       }
                                   });
    }

    if (descriptorCount != 1 || owningDescriptor == nullptr)
    {
        throw std::invalid_argument{"Inverse many-to-many relation '" + inverseDescriptor.fieldName() +
                                    "' does not reference exactly one owning descriptor"};
    }

    auto owningJunction = makeOwningJunction<Target, Current>(*owningDescriptor);
    return JunctionInfo{owningJunction.tableName,
                        std::move(owningJunction.targetColumns),
                        std::move(owningJunction.ownerColumns),
                        false};
}

template <typename Owner, typename Collection>
auto makeCollectionRelation(const std::string& fieldName) -> RelationInfo
{
    using collection_t = std::remove_cv_t<Collection>;
    using target_t = relation_target_t<collection_t>;

    (void)requirePrimaryKey<Owner>(fieldName);
    (void)requirePrimaryKey<target_t>(fieldName);

    RelationInfo result;
    result.fieldName = fieldName;
    result.columnName = fieldName;
    result.kind = is_one_to_many_v<collection_t> ? RelationKind::OneToMany : RelationKind::ManyToMany;
    result.targetType = typeid(target_t);
    result.targetModelResolver = []() -> const ModelInfo& { return getCachedModelInfo<target_t>(); };

    std::size_t descriptorCount = 0;
    bool descriptorKindMatches = false;
    if constexpr (requires { Owner::relations; })
    {
        utils::constexpr_for_tuple(Owner::relations, [&](auto, const auto& descriptor)
                                   {
                                       if (descriptor.fieldName() != fieldName)
                                       {
                                           return;
                                       }

                                       ++descriptorCount;
                                       using descriptor_t = std::decay_t<decltype(descriptor)>;
                                       if constexpr (is_one_to_many_v<collection_t> &&
                                                     std::is_same_v<descriptor_t, OneToManyDescriptor>)
                                       {
                                           descriptorKindMatches = true;
                                           result.mappedBy = descriptor.mappedByField();
                                           result.nullable = validateOneToManyMappedBy<Owner, target_t>(descriptor);
                                       }
                                       else if constexpr (not is_one_to_many_v<collection_t> &&
                                                          std::is_same_v<descriptor_t, ManyToManyDescriptor>)
                                       {
                                           descriptorKindMatches = true;
                                           result.mappedBy = descriptor.mappedByField();
                                           result.junction = descriptor.mappedByField().empty()
                                                                 ? makeOwningJunction<Owner, target_t>(descriptor)
                                                                 : makeInverseJunction<Owner, target_t>(descriptor);
                                       }
                                   });
    }

    if (descriptorCount != 1 || not descriptorKindMatches)
    {
        throw std::invalid_argument{"Collection field '" + fieldName +
                                    "' must have exactly one matching relation descriptor"};
    }
    return result;
}

template <typename Owner, typename Field>
auto appendToOneRelation(const std::string& fieldName, const ColumnInfo& column, ModelInfo& modelInfo) -> void
{
    using field_t = std::remove_cv_t<Field>;
    using target_t = optional_value_t<field_t>;

    if constexpr (hasIdDefinition<target_t>())
    {
        if (getPrimaryIdColumnsNames<target_t>().empty())
        {
            return;
        }

        RelationInfo relation;
        relation.fieldName = fieldName;
        relation.columnName = column.name;
        relation.kind = RelationKind::ToOne;
        relation.nullable = OptionalTraits<field_t>::isOptional;
        relation.targetType = typeid(target_t);
        relation.targetModelResolver = []() -> const ModelInfo& { return getCachedModelInfo<target_t>(); };
        modelInfo.relationsInfo.push_back(std::move(relation));
    }
}

template <typename T>
auto validateDescriptors(const std::unordered_set<std::string>& collectionFields) -> void
{
    std::unordered_set<std::string> descriptorFields;
    std::unordered_set<std::string> junctionTables;
    if constexpr (requires { T::relations; })
    {
        utils::constexpr_for_tuple(T::relations, [&](auto, const auto& descriptor)
                                   {
                                       if (descriptor.fieldName().empty() ||
                                           not descriptorFields.insert(descriptor.fieldName()).second)
                                       {
                                           throw std::invalid_argument{
                                               "Every collection field must have exactly one relation descriptor"};
                                       }
                                       if (not collectionFields.contains(descriptor.fieldName()))
                                       {
                                           throw std::invalid_argument{"Relation descriptor '" + descriptor.fieldName() +
                                                                       "' does not name a collection field"};
                                       }

                                       using descriptor_t = std::decay_t<decltype(descriptor)>;
                                       if constexpr (std::is_same_v<descriptor_t, ManyToManyDescriptor>)
                                       {
                                           if (not descriptor.throughTable().empty() &&
                                               not junctionTables.insert(descriptor.throughTable()).second)
                                           {
                                               throw std::invalid_argument{"Conflicting owning relations use junction table '" +
                                                                           descriptor.throughTable() + "'"};
                                           }
                                       }
                                   });
    }

    if (descriptorFields.size() != collectionFields.size())
    {
        throw std::invalid_argument{"Every collection field must have exactly one relation descriptor"};
    }
}

struct JunctionRegistration
{
    std::type_index ownerType{typeid(void)};
    std::type_index targetType{typeid(void)};
    std::string fieldName;
    std::vector<std::string> ownerColumns;
    std::vector<std::string> targetColumns;
};

inline auto validateGlobalJunctionMappings(std::type_index ownerType, const ModelInfo& modelInfo) -> void
{
    static std::mutex registryMutex;
    static std::unordered_map<std::string, JunctionRegistration> registry;

    std::vector<std::pair<std::string, JunctionRegistration>> candidates;
    for (const auto& relation : modelInfo.relationsInfo)
    {
        if (relation.kind != RelationKind::ManyToMany || not relation.junction.has_value() ||
            not relation.junction->owningSide)
        {
            continue;
        }

        const auto& junction = relation.junction.value();
        candidates.emplace_back(
            junction.tableName,
            JunctionRegistration{ownerType, relation.targetType, relation.fieldName, junction.ownerColumns,
                                 junction.targetColumns});
    }

    const std::scoped_lock lock{registryMutex};
    for (const auto& [tableName, candidate] : candidates)
    {
        const auto existing = registry.find(tableName);
        if (existing == registry.end())
        {
            continue;
        }

        const auto& registered = existing->second;
        const auto sameMapping = registered.ownerType == candidate.ownerType &&
                                 registered.targetType == candidate.targetType &&
                                 registered.fieldName == candidate.fieldName &&
                                 registered.ownerColumns == candidate.ownerColumns &&
                                 registered.targetColumns == candidate.targetColumns;
        if (not sameMapping)
        {
            throw std::invalid_argument{"Conflicting owning relations use junction table '" + tableName + "'"};
        }
    }

    for (auto& [tableName, candidate] : candidates)
    {
        registry.try_emplace(std::move(tableName), std::move(candidate));
    }
}
} // namespace detail
} // namespace orm::model
