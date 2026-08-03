#pragma once

#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>

#include "ColumnType.hpp"
#include "RelationKind.hpp"

namespace orm::model
{
struct TypeId
{
    const void* value{};

    constexpr auto operator==(const TypeId&) const noexcept -> bool = default;
};

namespace detail
{
template <typename T>
inline constexpr unsigned char typeToken{};
} // namespace detail

template <typename T>
consteval auto typeId() noexcept -> TypeId
{
    return TypeId{&detail::typeToken<T>};
}

enum class FieldKind
{
    Scalar,
    ToOne,
};

inline constexpr auto noTargetModel = std::numeric_limits<std::size_t>::max();

struct ColumnView
{
    std::size_t fieldIndex{};
    std::string_view fieldName;
    std::string_view name;
    /**
     * Scalar logical type. To-one fields intentionally have no single type:
     * their physical foreign-key columns inherit the types of the target
     * model's primary-key columns.
     */
    std::optional<ColumnType> type;
    FieldKind kind{FieldKind::Scalar};
    std::size_t targetModelIndex{noTargetModel};
    bool isPrimaryKey{};
    bool isAutoIncrement{};
    bool isNotNull{};

    constexpr auto operator==(const ColumnView&) const noexcept -> bool = default;
};

struct JunctionView
{
    std::string_view tableName;
    std::span<const std::string_view> ownerColumns;
    std::span<const std::string_view> targetColumns;
    bool owningSide{};

    [[nodiscard]] constexpr auto isConfigured() const noexcept -> bool
    {
        return not tableName.empty();
    }
};

struct RelationView
{
    std::size_t fieldIndex{};
    std::string_view fieldName;
    std::string_view columnName;
    RelationKind kind{RelationKind::ToOne};
    std::string_view mappedBy;
    bool nullable{};
    std::size_t targetModelIndex{noTargetModel};
    JunctionView junction{};
};

/**
 * @brief Immutable data stored once for a model inside a closed schema.
 */
struct ModelDataView
{
    TypeId type;
    std::size_t schemaIndex{};
    std::string_view typeName;
    std::string_view tableName;
    std::span<const ColumnView> columns;
    std::span<const std::size_t> primaryKeyIndices;
    std::span<const RelationView> relations;

    [[nodiscard]] constexpr auto findColumn(std::string_view fieldOrSqlName) const noexcept -> const ColumnView*
    {
        for (const auto& column : columns)
        {
            if (column.fieldName == fieldOrSqlName || column.name == fieldOrSqlName)
            {
                return &column;
            }
        }
        return nullptr;
    }

    [[nodiscard]] constexpr auto findRelation(std::string_view fieldOrSqlName) const noexcept -> const RelationView*
    {
        for (const auto& relation : relations)
        {
            if (relation.fieldName == fieldOrSqlName)
            {
                return &relation;
            }
        }
        for (const auto& relation : relations)
        {
            if (relation.columnName == fieldOrSqlName)
            {
                return &relation;
            }
        }
        return nullptr;
    }

    [[nodiscard]] constexpr auto findRelationField(std::string_view fieldName) const noexcept -> const RelationView*
    {
        for (const auto& relation : relations)
        {
            if (relation.fieldName == fieldName)
            {
                return &relation;
            }
        }
        return nullptr;
    }
};

struct SchemaView;

/**
 * @brief Trivially-copyable handle to one model in immutable Schema storage.
 */
struct ModelView
{
    const SchemaView* schema{};
    std::size_t modelIndex{noTargetModel};

    [[nodiscard]] constexpr auto valid() const noexcept -> bool;
    [[nodiscard]] constexpr explicit operator bool() const noexcept;
    [[nodiscard]] constexpr auto operator==(std::nullptr_t) const noexcept -> bool;
    [[nodiscard]] constexpr auto operator==(const ModelView&) const noexcept -> bool = default;
    [[nodiscard]] constexpr auto operator*() const noexcept -> const ModelView&;
    [[nodiscard]] constexpr auto operator->() const noexcept -> const ModelDataView*;
    [[nodiscard]] constexpr auto data() const noexcept -> const ModelDataView&;

    [[nodiscard]] constexpr auto findColumn(std::string_view fieldOrSqlName) const noexcept -> const ColumnView*;
    [[nodiscard]] constexpr auto findRelation(std::string_view fieldOrSqlName) const noexcept -> const RelationView*;
    [[nodiscard]] constexpr auto findRelationField(std::string_view fieldName) const noexcept -> const RelationView*;
    [[nodiscard]] constexpr auto resolveTarget(const ColumnView& column) const noexcept -> ModelView;
    [[nodiscard]] constexpr auto resolveTarget(const RelationView& relation) const noexcept -> ModelView;
    [[nodiscard]] constexpr auto resolveJunction(const RelationView& relation) const noexcept -> JunctionView;
    [[nodiscard]] constexpr auto primaryKeySize() const noexcept -> std::size_t;
};

/**
 * @brief Immutable view over a statically materialized schema.
 */
struct SchemaView
{
    std::span<const ModelDataView* const> models;

    [[nodiscard]] constexpr auto at(std::size_t index) const noexcept -> ModelView
    {
        return ModelView{this, index < models.size() ? index : noTargetModel};
    }

    [[nodiscard]] constexpr auto find(TypeId type) const noexcept -> ModelView
    {
        for (std::size_t index = 0; index < models.size(); ++index)
        {
            if (models[index]->type == type)
            {
                return at(index);
            }
        }
        return ModelView{this, noTargetModel};
    }

    [[nodiscard]] constexpr auto findTable(std::string_view tableName) const noexcept -> ModelView
    {
        for (std::size_t index = 0; index < models.size(); ++index)
        {
            if (models[index]->tableName == tableName)
            {
                return at(index);
            }
        }
        return ModelView{this, noTargetModel};
    }
};

constexpr auto ModelView::valid() const noexcept -> bool
{
    return schema != nullptr && modelIndex < schema->models.size() && schema->models[modelIndex] != nullptr;
}

constexpr ModelView::operator bool() const noexcept
{
    return valid();
}

constexpr auto ModelView::operator==(std::nullptr_t) const noexcept -> bool
{
    return not valid();
}

constexpr auto ModelView::operator*() const noexcept -> const ModelView&
{
    return *this;
}

constexpr auto ModelView::operator->() const noexcept -> const ModelDataView*
{
    return valid() ? schema->models[modelIndex] : nullptr;
}

constexpr auto ModelView::data() const noexcept -> const ModelDataView&
{
    return *schema->models[modelIndex];
}

constexpr auto ModelView::findColumn(std::string_view fieldOrSqlName) const noexcept -> const ColumnView*
{
    return valid() ? data().findColumn(fieldOrSqlName) : nullptr;
}

constexpr auto ModelView::findRelation(std::string_view fieldOrSqlName) const noexcept -> const RelationView*
{
    return valid() ? data().findRelation(fieldOrSqlName) : nullptr;
}

constexpr auto ModelView::findRelationField(std::string_view fieldName) const noexcept -> const RelationView*
{
    return valid() ? data().findRelationField(fieldName) : nullptr;
}

constexpr auto ModelView::resolveTarget(const ColumnView& column) const noexcept -> ModelView
{
    if (not valid() || column.kind != FieldKind::ToOne || column.targetModelIndex == noTargetModel)
    {
        return {};
    }
    return schema->at(column.targetModelIndex);
}

constexpr auto ModelView::resolveTarget(const RelationView& relation) const noexcept -> ModelView
{
    if (not valid() || relation.targetModelIndex == noTargetModel)
    {
        return {};
    }
    return schema->at(relation.targetModelIndex);
}

constexpr auto ModelView::resolveJunction(const RelationView& relation) const noexcept -> JunctionView
{
    if (relation.junction.isConfigured())
    {
        return relation.junction;
    }
    if (relation.kind != RelationKind::ManyToMany || relation.mappedBy.empty())
    {
        return {};
    }

    const auto target = resolveTarget(relation);
    const auto* owningRelation = target ? target.findRelationField(relation.mappedBy) : nullptr;
    if (owningRelation == nullptr || owningRelation->kind != RelationKind::ManyToMany ||
        not owningRelation->junction.isConfigured())
    {
        return {};
    }

    return JunctionView{
        .tableName = owningRelation->junction.tableName,
        .ownerColumns = owningRelation->junction.targetColumns,
        .targetColumns = owningRelation->junction.ownerColumns,
        .owningSide = false,
    };
}

constexpr auto ModelView::primaryKeySize() const noexcept -> std::size_t
{
    return valid() ? data().primaryKeyIndices.size() : 0U;
}

static_assert(std::is_trivially_copyable_v<ModelView>);
static_assert(sizeof(ModelView) == sizeof(const SchemaView*) + sizeof(std::size_t));
static_assert(std::is_trivially_copyable_v<SchemaView>);
} // namespace orm::model
