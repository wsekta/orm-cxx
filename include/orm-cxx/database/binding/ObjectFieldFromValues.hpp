#pragma once

#include <format>
#include <optional>
#include <string>
#include <type_traits>

#include "BindingConcepts.hpp"
#include "BindingPayload.hpp"
#include "ConversionError.hpp"
#include "NumericValue.hpp"
#include "orm-cxx/reflection/Reflection.hpp"
#include "orm-cxx/relations.hpp"
#include "orm-cxx/utils/ConstexprFor.hpp"
#include "soci/values.h"

namespace orm::db::binding
{
template <typename T>
struct IsOptionalBoundField : std::false_type
{
};

template <typename T>
struct IsOptionalBoundField<std::optional<T>> : std::true_type
{
    using value_type = T;
};

template <typename ModelField>
auto getScalarFieldValue(const soci::values& values, const std::string& fieldName) -> ModelField
{
    if constexpr (IsOptionalBoundField<ModelField>::value)
    {
        if (values.get_indicator(fieldName) == soci::i_null)
        {
            return std::nullopt;
        }

        using value_type = typename IsOptionalBoundField<ModelField>::value_type;
        return getScalarFieldValue<value_type>(values, fieldName);
    }
    else if constexpr (std::is_arithmetic_v<ModelField>)
    {
        return getNumericValue<ModelField>(values, fieldName);
    }
    else if constexpr (SociDefaultSupported<ModelField>)
    {
        return values.get<ModelField>(fieldName);
    }
    else
    {
        throw ConversionError{"Unsupported related model field type: " + fieldName};
    }
}

namespace detail
{
template <typename Owner>
[[nodiscard]] auto selectedScalarAlias(const model::ModelView& owner, const model::ColumnView& column) -> std::string
{
    return std::format("{}_{}", owner->tableName, column.name);
}

template <bool JoinedValues>
[[nodiscard]] auto selectedRelatedAlias(const model::ModelView& owner, const model::ColumnView& relationColumn,
                                        const model::ColumnView& targetColumn) -> std::string
{
    if constexpr (JoinedValues)
    {
        return std::format("{}_{}", relationColumn.name, targetColumn.name);
    }
    else
    {
        return std::format("{}_{}_{}", owner->tableName, relationColumn.name, targetColumn.name);
    }
}

template <typename Related, typename SchemaType, bool JoinedValues>
auto hydrateRelated(Related& related, const model::ModelView& owner, const model::ColumnView& relationColumn,
                    const soci::values& values) -> void
{
    using related_t = std::remove_cv_t<Related>;
    auto relatedFields = reflection::fieldPointers(related);

    auto hydrate = [&]<typename Index>(Index, auto* field)
    {
        using field_t = std::decay_t<decltype(*field)>;
        if constexpr (orm::is_relation_collection_v<field_t>)
        {
            return;
        }
        else
        {
            constexpr auto* targetColumn = mappedColumn<related_t, SchemaType, Index::value>;
            if constexpr (targetColumn != nullptr)
            {
                if constexpr (JoinedValues)
                {
                    const auto alias = selectedRelatedAlias<true>(owner, relationColumn, *targetColumn);
                    *field = getScalarFieldValue<field_t>(values, alias);
                }
                else if (targetColumn->isPrimaryKey)
                {
                    const auto alias = selectedRelatedAlias<false>(owner, relationColumn, *targetColumn);
                    *field = getScalarFieldValue<field_t>(values, alias);
                }
            }
        }
    };

    utils::constexpr_for_tuple(relatedFields, hydrate);
}

template <typename Related, typename SchemaType, bool JoinedValues>
[[nodiscard]] auto relatedPrimaryKeyIsPresent(const model::ModelView& owner, const model::ColumnView& relationColumn,
                                              const soci::values& values) -> bool
{
    constexpr auto related = model::modelView<SchemaType, Related>();
    bool hasPresentPrimaryKey{};
    bool hasNullPrimaryKey{};

    for (const auto primaryKeyIndex : related->primaryKeyIndices)
    {
        const auto& targetColumn = related->columns[primaryKeyIndex];
        const auto alias = selectedRelatedAlias<JoinedValues>(owner, relationColumn, targetColumn);
        if (values.get_indicator(alias) == soci::i_null)
        {
            hasNullPrimaryKey = true;
        }
        else
        {
            hasPresentPrimaryKey = true;
        }
    }

    if (not hasPresentPrimaryKey)
    {
        return false;
    }
    if (hasNullPrimaryKey)
    {
        throw ConversionError{"Cannot hydrate optional relation with a partially null primary key"};
    }
    return true;
}
} // namespace detail

template <typename ModelField>
struct ObjectFieldFromValues
{
    template <typename T, typename SchemaType, bool JoinedValues>
    static auto get(ModelField* field, const BindingPayload<T, SchemaType, JoinedValues>&,
                    const model::ColumnView& column, const soci::values& values) -> void
    {
        constexpr auto owner = model::modelView<SchemaType, T>();

        if constexpr (SchemaModel<SchemaType, ModelField>)
        {
            using related_t = std::remove_cv_t<optional_value_t<ModelField>>;

            if constexpr (IsOptionalBoundField<ModelField>::value)
            {
                if (not detail::relatedPrimaryKeyIsPresent<related_t, SchemaType, JoinedValues>(owner, column, values))
                {
                    *field = std::nullopt;
                    return;
                }
                detail::hydrateRelated<related_t, SchemaType, JoinedValues>(field->emplace(), owner, column, values);
            }
            else
            {
                detail::hydrateRelated<ModelField, SchemaType, JoinedValues>(*field, owner, column, values);
            }
        }
        else
        {
            const auto alias = detail::selectedScalarAlias<T>(owner, column);
            *field = getScalarFieldValue<ModelField>(values, alias);
        }
    }

    template <typename T, typename SchemaType, bool JoinedValues, std::size_t FieldIndex>
    static auto get(ModelField* field, const BindingPayload<T, SchemaType, JoinedValues>& payload,
                    std::integral_constant<std::size_t, FieldIndex>, const soci::values& values) -> void
    {
        get(field, payload, payload.template columnDescriptor<FieldIndex>(), values);
    }

    template <typename T, typename SchemaType, bool JoinedValues>
    static auto get(ModelField* field, const BindingPayload<T, SchemaType, JoinedValues>& payload,
                    std::size_t fieldIndex, const soci::values& values) -> void
    {
        get(field, payload, payload.columnDescriptor(fieldIndex), values);
    }
};
} // namespace orm::db::binding
