#pragma once

#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>

#include "BindingConcepts.hpp"
#include "BindingPayload.hpp"
#include "NullBinding.hpp"
#include "NumericConversion.hpp"
#include "orm-cxx/reflection/Reflection.hpp"
#include "orm-cxx/relations.hpp"
#include "orm-cxx/utils/ConstexprFor.hpp"
#include "soci/values.h"

namespace orm::db::binding
{
inline auto setNullValue(soci::values& values, const std::string& name, model::ColumnType type) -> void
{
    switch (type)
    {
    case model::ColumnType::Bool:
    case model::ColumnType::Char:
    case model::ColumnType::UnsignedChar:
    case model::ColumnType::Short:
    case model::ColumnType::UnsignedShort:
    case model::ColumnType::Int:
        bindTypedNull(values, name, int{});
        return;
    case model::ColumnType::UnsignedInt:
    case model::ColumnType::UnsignedLongLong:
        bindTypedNull(values, name, static_cast<unsigned long long>(0));
        return;
    case model::ColumnType::LongLong:
        bindTypedNull(values, name, static_cast<long long>(0));
        return;
    case model::ColumnType::Float:
    case model::ColumnType::Double:
        bindTypedNull(values, name, 0.0);
        return;
    case model::ColumnType::String:
        bindTypedNull(values, name, std::string{});
        return;
    case model::ColumnType::Uuid:
        throw std::invalid_argument{"Cannot bind NULL value with unsupported column type"};
    }

    throw std::invalid_argument{"Cannot bind NULL value with unsupported column type"};
}

template <typename T>
struct IsOptionalScalarField : std::false_type
{
};

template <typename T>
struct IsOptionalScalarField<std::optional<T>> : std::true_type
{
    using value_type = T;
};

template <typename ModelField>
auto setScalarFieldValue(soci::values& values, const std::string& fieldName, const ModelField& field,
                         model::ColumnType columnType) -> void
{
    if constexpr (IsOptionalScalarField<ModelField>::value)
    {
        if (not field.has_value())
        {
            setNullValue(values, fieldName, columnType);
            return;
        }

        setScalarFieldValue(values, fieldName, field.value(), columnType);
    }
    else if constexpr (SociConvertableToDouble<ModelField>)
    {
        values.set(fieldName, checkedNumericCast<double>(field, fieldName));
    }
    else if constexpr (SociConvertableToInt<ModelField>)
    {
        values.set(fieldName, checkedNumericCast<int>(field, fieldName));
    }
    else if constexpr (SociConvertableToLongLong<ModelField>)
    {
        values.set(fieldName, checkedNumericCast<long long>(field, fieldName));
    }
    else if constexpr (SociConvertableToUnsignedLongLong<ModelField>)
    {
        values.set(fieldName, checkedNumericCast<unsigned long long>(field, fieldName));
    }
    else if constexpr (SociDefaultSupported<ModelField>)
    {
        if constexpr (std::is_arithmetic_v<ModelField>)
        {
            values.set(fieldName, checkedNumericCast<ModelField>(field, fieldName));
        }
        else
        {
            values.set(fieldName, field);
        }
    }
    else
    {
        throw std::invalid_argument{"Unsupported related model field type: " + fieldName};
    }
}

namespace detail
{
template <typename Related, typename SchemaType>
auto serializeRelated(const Related& related, const model::ColumnView& relationColumn, soci::values& values) -> void
{
    using related_t = std::remove_cv_t<Related>;
    const auto relatedFields = reflection::fieldPointers(related);

    auto serialize = [&]<typename Index>(Index, const auto* field)
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
                if (targetColumn->isPrimaryKey)
                {
                    const auto parameterName = std::format("{}_{}", relationColumn.name, targetColumn->name);
                    setScalarFieldValue(values, parameterName, *field, targetColumn->type.value());
                }
            }
        }
    };

    utils::constexpr_for_tuple(relatedFields, serialize);
}

template <typename Related, typename SchemaType>
auto setRelatedNull(soci::values& values, const model::ColumnView& relationColumn) -> void
{
    constexpr auto related = model::modelView<SchemaType, Related>();
    for (const auto primaryKeyIndex : related->primaryKeyIndices)
    {
        const auto& targetColumn = related->columns[primaryKeyIndex];
        setNullValue(values, std::format("{}_{}", relationColumn.name, targetColumn.name), targetColumn.type.value());
    }
}
} // namespace detail

template <typename ModelField>
struct ObjectFieldToValues
{
    template <typename T, typename SchemaType, bool JoinedValues>
    static auto set(const ModelField* field, const BindingPayload<T, SchemaType, JoinedValues>&,
                    const model::ColumnView& column, soci::values& values) -> void
    {
        if (column.isAutoIncrement)
        {
            return;
        }

        if constexpr (SchemaModel<SchemaType, ModelField>)
        {
            using related_t = std::remove_cv_t<optional_value_t<ModelField>>;
            if constexpr (IsOptionalScalarField<ModelField>::value)
            {
                if (field->has_value())
                {
                    detail::serializeRelated<related_t, SchemaType>(field->value(), column, values);
                }
                else
                {
                    detail::setRelatedNull<related_t, SchemaType>(values, column);
                }
            }
            else
            {
                detail::serializeRelated<ModelField, SchemaType>(*field, column, values);
            }
        }
        else
        {
            setScalarFieldValue(values, std::string{column.name}, *field, column.type.value());
        }
    }

    template <typename T, typename SchemaType, bool JoinedValues, std::size_t FieldIndex>
    static auto set(const ModelField* field, const BindingPayload<T, SchemaType, JoinedValues>& payload,
                    std::integral_constant<std::size_t, FieldIndex>, soci::values& values) -> void
    {
        set(field, payload, payload.template columnDescriptor<FieldIndex>(), values);
    }

    template <typename T, typename SchemaType, bool JoinedValues>
    static auto set(const ModelField* field, const BindingPayload<T, SchemaType, JoinedValues>& payload,
                    std::size_t fieldIndex, soci::values& values) -> void
    {
        set(field, payload, payload.columnDescriptor(fieldIndex), values);
    }
};
} // namespace orm::db::binding
