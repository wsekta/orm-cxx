module;

#include <array>
#include <concepts>
#include <cstddef>
#include <span>
#include <string_view>
#include <type_traits>

export module orm:model_schema;

import orm.reflection;
import :foundation;
import :model_mapping;
import :model_views;
import :model_columns;
import :model_relations;

namespace orm::model
{
namespace detail
{
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
}
export
{
    // namespace detail

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
        static_assert(detail::hasValidAutoIncrement(columns),
                      "ORM_MODEL_AUTO_INCREMENT: auto increment requires one "
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
}
} // namespace orm::model

// model/Schema.hpp
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
            if (names[left].compare(names[right]) == 0)
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
        for (std::size_t relationIndex = 0; relationIndex < owner->relations.size(); ++relationIndex)
        {
            const auto& relation = owner->relations[relationIndex];
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
        for (std::size_t scalarIndex = 0; scalarIndex < owner->columns.size(); ++scalarIndex)
        {
            const auto& scalar = owner->columns[scalarIndex];
            if (scalar.kind != FieldKind::Scalar)
            {
                continue;
            }
            for (std::size_t relationColumnIndex = 0; relationColumnIndex < owner->columns.size();
                 ++relationColumnIndex)
            {
                const auto& relation = owner->columns[relationColumnIndex];
                if (relation.kind != FieldKind::ToOne || relation.targetModelIndex >= models.size())
                {
                    continue;
                }
                const auto* target = models[relation.targetModelIndex];
                for (std::size_t keyIndex = 0; keyIndex < target->primaryKeyIndices.size(); ++keyIndex)
                {
                    const auto primaryKeyIndex = target->primaryKeyIndices[keyIndex];
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
            for (std::size_t leftKeyIndex = 0; leftKeyIndex < leftTarget->primaryKeyIndices.size(); ++leftKeyIndex)
            {
                const auto leftPrimaryKeyIndex = leftTarget->primaryKeyIndices[leftKeyIndex];
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
                    for (std::size_t rightKeyIndex = 0; rightKeyIndex < rightTarget->primaryKeyIndices.size();
                         ++rightKeyIndex)
                    {
                        const auto rightPrimaryKeyIndex = rightTarget->primaryKeyIndices[rightKeyIndex];
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
            if (left[leftIndex].compare(left[other]) == 0)
            {
                return false;
            }
        }
        for (std::size_t rightIndex = 0; rightIndex < right.size(); ++rightIndex)
        {
            const auto rightName = right[rightIndex];
            if (rightName.empty() || left[leftIndex].compare(rightName) == 0)
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
            if (right[rightIndex].compare(right[other]) == 0)
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
                if (model->tableName.compare(relation.junction.tableName) == 0)
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
                        other.junction.tableName.compare(relation.junction.tableName) == 0)
                    {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}
}
export
{
    // namespace detail

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
            static_assert(contains<Model>,
                          "ORM_SCHEMA_MODEL_MISSING: requested model type does not belong to this schema");
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
        static_assert(
            detail::hasValidJunctions(modelStorage),
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
}
} // namespace orm::model

namespace orm
{
export
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
}
} // namespace orm
