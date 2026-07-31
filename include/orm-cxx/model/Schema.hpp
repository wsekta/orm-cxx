#pragma once

#include <array>
#include <cstddef>
#include <string_view>
#include <type_traits>

#include "StaticModel.hpp"

namespace orm::model
{
namespace detail
{
template <typename... Models>
consteval auto hasUniqueTableNames() -> bool
{
    constexpr std::array<std::string_view, sizeof...(Models)> names{tableName<Models>()...};
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

template <std::size_t Size>
consteval auto hasValidRelationTargets(const std::array<const ModelDataView*, Size>& models) -> bool
{
    for (const auto* owner : models)
    {
        for (const auto& relation : owner->relations)
        {
            if (relation.targetModelIndex >= models.size())
            {
                return false;
            }
            const auto* target = models[relation.targetModelIndex];
            if (target == nullptr || target->primaryKeyIndices.empty())
            {
                return false;
            }

            if (relation.kind == RelationKind::OneToMany)
            {
                if (relation.mappedBy.empty())
                {
                    return false;
                }
                const auto* inverse = target->findRelationField(relation.mappedBy);
                if (inverse == nullptr || inverse->kind != RelationKind::ToOne ||
                    inverse->targetModelIndex != owner->schemaIndex)
                {
                    return false;
                }
            }
            else if (relation.kind == RelationKind::ManyToMany && not relation.junction.isConfigured())
            {
                if (relation.mappedBy.empty())
                {
                    return false;
                }
                const auto* owning = target->findRelationField(relation.mappedBy);
                if (owning == nullptr || owning->kind != RelationKind::ManyToMany ||
                    owning->targetModelIndex != owner->schemaIndex || not owning->junction.isConfigured() ||
                    not owning->junction.owningSide)
                {
                    return false;
                }
            }
        }
    }
    return true;
}

[[nodiscard]] consteval auto foreignKeyColumnNameSize(std::string_view relationName,
                                                      std::string_view targetColumnName) -> std::size_t
{
    return relationName.size() + 1U + targetColumnName.size();
}

[[nodiscard]] consteval auto foreignKeyColumnNameCharacter(std::string_view relationName,
                                                           std::string_view targetColumnName, std::size_t index) -> char
{
    if (index < relationName.size())
    {
        return relationName[index];
    }
    if (index == relationName.size())
    {
        return '_';
    }
    return targetColumnName[index - relationName.size() - 1U];
}

[[nodiscard]] consteval auto equalsForeignKeyColumnName(std::string_view candidate, std::string_view relationName,
                                                        std::string_view targetColumnName) -> bool
{
    if (candidate.size() != foreignKeyColumnNameSize(relationName, targetColumnName))
    {
        return false;
    }
    for (std::size_t index = 0; index < candidate.size(); ++index)
    {
        if (candidate[index] != foreignKeyColumnNameCharacter(relationName, targetColumnName, index))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] consteval auto equalForeignKeyColumnNames(std::string_view leftRelation,
                                                        std::string_view leftTargetColumn,
                                                        std::string_view rightRelation,
                                                        std::string_view rightTargetColumn) -> bool
{
    const auto size = foreignKeyColumnNameSize(leftRelation, leftTargetColumn);
    if (size != foreignKeyColumnNameSize(rightRelation, rightTargetColumn))
    {
        return false;
    }
    for (std::size_t index = 0; index < size; ++index)
    {
        if (foreignKeyColumnNameCharacter(leftRelation, leftTargetColumn, index) !=
            foreignKeyColumnNameCharacter(rightRelation, rightTargetColumn, index))
        {
            return false;
        }
    }
    return true;
}

template <std::size_t Size>
consteval auto hasUniquePhysicalColumnNames(const std::array<const ModelDataView*, Size>& models) -> bool
{
    for (const auto* owner : models)
    {
        for (const auto& scalar : owner->columns)
        {
            if (scalar.kind != FieldKind::Scalar)
            {
                continue;
            }
            for (const auto& relation : owner->columns)
            {
                if (relation.kind != FieldKind::ToOne || relation.targetModelIndex >= models.size())
                {
                    continue;
                }
                const auto* target = models[relation.targetModelIndex];
                for (const auto primaryKeyIndex : target->primaryKeyIndices)
                {
                    if (primaryKeyIndex >= target->columns.size() ||
                        equalsForeignKeyColumnName(scalar.name, relation.name, target->columns[primaryKeyIndex].name))
                    {
                        return false;
                    }
                }
            }
        }

        for (std::size_t leftRelationIndex = 0; leftRelationIndex < owner->columns.size(); ++leftRelationIndex)
        {
            const auto& leftRelation = owner->columns[leftRelationIndex];
            if (leftRelation.kind != FieldKind::ToOne || leftRelation.targetModelIndex >= models.size())
            {
                continue;
            }
            const auto* leftTarget = models[leftRelation.targetModelIndex];
            for (const auto leftPrimaryKeyIndex : leftTarget->primaryKeyIndices)
            {
                if (leftPrimaryKeyIndex >= leftTarget->columns.size())
                {
                    return false;
                }
                for (std::size_t rightRelationIndex = leftRelationIndex + 1; rightRelationIndex < owner->columns.size();
                     ++rightRelationIndex)
                {
                    const auto& rightRelation = owner->columns[rightRelationIndex];
                    if (rightRelation.kind != FieldKind::ToOne || rightRelation.targetModelIndex >= models.size())
                    {
                        continue;
                    }
                    const auto* rightTarget = models[rightRelation.targetModelIndex];
                    for (const auto rightPrimaryKeyIndex : rightTarget->primaryKeyIndices)
                    {
                        if (rightPrimaryKeyIndex >= rightTarget->columns.size() ||
                            equalForeignKeyColumnNames(leftRelation.name, leftTarget->columns[leftPrimaryKeyIndex].name,
                                                       rightRelation.name,
                                                       rightTarget->columns[rightPrimaryKeyIndex].name))
                        {
                            return false;
                        }
                    }
                }
            }
        }
    }
    return true;
}

[[nodiscard]] consteval auto hasUniqueNames(std::span<const std::string_view> left,
                                            std::span<const std::string_view> right = {}) -> bool
{
    for (std::size_t leftIndex = 0; leftIndex < left.size(); ++leftIndex)
    {
        if (left[leftIndex].empty())
        {
            return false;
        }
        for (std::size_t other = leftIndex + 1; other < left.size(); ++other)
        {
            if (left[leftIndex] == left[other])
            {
                return false;
            }
        }
        for (const auto rightName : right)
        {
            if (rightName.empty() || left[leftIndex] == rightName)
            {
                return false;
            }
        }
    }
    for (std::size_t rightIndex = 0; rightIndex < right.size(); ++rightIndex)
    {
        if (right[rightIndex].empty())
        {
            return false;
        }
        for (std::size_t other = rightIndex + 1; other < right.size(); ++other)
        {
            if (right[rightIndex] == right[other])
            {
                return false;
            }
        }
    }
    return true;
}

template <std::size_t Size>
consteval auto hasValidJunctions(const std::array<const ModelDataView*, Size>& models) -> bool
{
    for (std::size_t ownerIndex = 0; ownerIndex < models.size(); ++ownerIndex)
    {
        const auto* owner = models[ownerIndex];
        for (std::size_t relationIndex = 0; relationIndex < owner->relations.size(); ++relationIndex)
        {
            const auto& relation = owner->relations[relationIndex];
            if (relation.kind != RelationKind::ManyToMany || not relation.junction.isConfigured())
            {
                continue;
            }
            const auto* target = models[relation.targetModelIndex];
            if (not relation.mappedBy.empty() || not relation.junction.owningSide ||
                relation.junction.ownerColumns.size() != owner->primaryKeyIndices.size() ||
                relation.junction.targetColumns.size() != target->primaryKeyIndices.size() ||
                not hasUniqueNames(relation.junction.ownerColumns, relation.junction.targetColumns))
            {
                return false;
            }
            for (const auto* model : models)
            {
                if (model->tableName == relation.junction.tableName)
                {
                    return false;
                }
            }
            for (std::size_t otherOwnerIndex = ownerIndex; otherOwnerIndex < models.size(); ++otherOwnerIndex)
            {
                const auto* otherOwner = models[otherOwnerIndex];
                const auto firstRelation = otherOwnerIndex == ownerIndex ? relationIndex + 1 : std::size_t{};
                for (std::size_t otherRelationIndex = firstRelation; otherRelationIndex < otherOwner->relations.size();
                     ++otherRelationIndex)
                {
                    const auto& other = otherOwner->relations[otherRelationIndex];
                    if (other.kind == RelationKind::ManyToMany && other.junction.isConfigured() &&
                        other.junction.tableName == relation.junction.tableName)
                    {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}
} // namespace detail

/**
 * @brief A closed compile-time set of ORM models.
 */
template <typename... Models>
struct Schema
{
    static_assert(detail::hasUniqueTypes<Models...>(),
                  "ORM_SCHEMA_DUPLICATE_MODEL: a schema may list a model type only once");
    static_assert(detail::hasUniqueTableNames<Models...>(),
                  "ORM_SCHEMA_DUPLICATE_TABLE: every model in a schema must use a unique table name");

    template <typename Model>
    inline static constexpr bool contains = (std::same_as<Model, Models> || ...);

    template <typename Model>
    [[nodiscard]] static consteval auto indexOf() -> std::size_t
    {
        static_assert(contains<Model>, "ORM_SCHEMA_MODEL_MISSING: requested model type does not belong to this schema");
        constexpr std::array<bool, sizeof...(Models)> matches{std::same_as<Model, Models>...};
        for (std::size_t index = 0; index < matches.size(); ++index)
        {
            if (matches[index])
            {
                return index;
            }
        }
        return 0;
    }

    [[nodiscard]] static constexpr auto modelAt(std::size_t index) noexcept -> ModelView
    {
        return view.at(index);
    }

    inline static constexpr std::array<const ModelDataView*, sizeof...(Models)> modelStorage{
        &StaticModel<Models, Schema<Models...>>::data...};

    static_assert(detail::hasUniquePhysicalColumnNames(modelStorage),
                  "ORM_SCHEMA_COLUMN_COLLISION: scalar and generated foreign-key column names must be unique");

    static_assert(detail::hasValidRelationTargets(modelStorage),
                  "ORM_SCHEMA_RELATION_GRAPH: mappedBy must resolve to a compatible relation in the closed schema");
    static_assert(detail::hasValidJunctions(modelStorage),
                  "ORM_SCHEMA_JUNCTION: junction tables and columns must be unique and match endpoint primary keys");

    inline static constexpr SchemaView view{.models = modelStorage};
};

template <typename SchemaType>
[[nodiscard]] constexpr auto schemaView() noexcept -> const SchemaView&
{
    return SchemaType::view;
}

template <typename SchemaType, typename T>
consteval auto requireSchemaModel() -> void
{
    static_assert(SchemaType::template contains<std::remove_cv_t<T>>,
                  "ORM_SCHEMA_MODEL_MISSING: requested model type does not belong to the closed schema");
}
} // namespace orm::model

namespace orm
{
template <typename... Models>
using Schema = model::Schema<Models...>;

template <typename SchemaType, typename T>
[[nodiscard]] consteval auto modelDescriptor() noexcept -> model::StaticModel<T, SchemaType>
{
    return model::modelDescriptor<SchemaType, T>();
}

template <typename SchemaType, typename T>
[[nodiscard]] constexpr auto modelView() noexcept -> model::ModelView
{
    return model::modelView<SchemaType, T>();
}
} // namespace orm
