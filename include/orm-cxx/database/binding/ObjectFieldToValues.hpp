#pragma once

#include <format>
#include <optional>
#include <stdexcept>
#include <type_traits>

#include "BindingConcepts.hpp"
#include "BindingPayload.hpp"
#include "NullBinding.hpp"
#include "NumericConversion.hpp"
#include "orm-cxx/relations.hpp"
#include "orm-cxx/utils/ConstexprFor.hpp"
#include "orm-cxx/utils/DisableExternalsWarning.hpp"
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
    case model::ColumnType::Unknown:
    case model::ColumnType::OneToOne:
        throw std::invalid_argument{"Cannot bind NULL value with unsupported column type"};
    }

    throw std::invalid_argument{"Cannot bind NULL value with unsupported column type"};
}

inline auto setOptionalNullValue(soci::values& values, const model::ModelInfo& modelInfo, std::size_t columnIndex)
    -> void
{
    const auto& columnInfo = modelInfo.columnsInfo[columnIndex];

    if (columnInfo.isForeignModel)
    {
        const auto& foreignModelInfo = modelInfo.foreignModelsInfo.at(columnInfo.name);

        for (const auto& foreignColumnInfo : foreignModelInfo.columnsInfo)
        {
            if (foreignColumnInfo.isPrimaryKey)
            {
                setNullValue(values, std::format("{}_{}", columnInfo.name, foreignColumnInfo.name),
                             foreignColumnInfo.type);
            }
        }

        return;
    }

    setNullValue(values, columnInfo.name, columnInfo.type);
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

template <typename ModelField>
struct ObjectFieldToValues;

template <SociDefaultSupported ModelField>
struct ObjectFieldToValues<ModelField>
{
    template <typename T, bool JoinedValues>
    static auto set(const ModelField* column, const BindingPayload<T, JoinedValues>& model, std::size_t columnIndex,
                    soci::values& values) -> void
    {
        const auto& columnInfo = model.getModelInfo().columnsInfo[columnIndex];

        if (columnInfo.isAutoIncrement)
        {
            return;
        }

        if constexpr (std::is_arithmetic_v<ModelField>)
        {
            values.set(columnInfo.name, checkedNumericCast<ModelField>(*column, columnInfo.name));
        }
        else
        {
            values.set(columnInfo.name, *column);
        }
    }
};

template <ModelWithId ModelField>
struct ObjectFieldToValues<ModelField>
{
    template <typename T, bool JoinedValues>
    static auto set(const ModelField* column, const BindingPayload<T, JoinedValues>& model, std::size_t columnIndex,
                    soci::values& values) -> void
    {
        auto foreignFieldName = model.getModelInfo().columnsInfo[columnIndex].name;
        const auto foreignModelAsTuple = rfl::to_view(*column).values();
        auto foreignModel = model.getModelInfo().foreignModelsInfo.at(foreignFieldName);
        std::size_t foreignColumnIndex = 0;

        auto setForeignFieldToValue = [&foreignModel, &values, &foreignFieldName,
                                       &foreignColumnIndex](auto /*fieldIndex*/, const auto foreignModelColumn)
        {
            using field_t = std::decay_t<decltype(*foreignModelColumn)>;

            if constexpr (orm::is_relation_collection_v<field_t>)
            {
                return;
            }
            else
            {
                const auto& columnInfo = foreignModel.columnsInfo[foreignColumnIndex++];

                if (columnInfo.isPrimaryKey)
                {
                    setScalarFieldValue(values, std::format("{}_{}", foreignFieldName, columnInfo.name),
                                        *foreignModelColumn, columnInfo.type);
                }
            }
        };

        orm::utils::constexpr_for_tuple(foreignModelAsTuple, setForeignFieldToValue);
    }
};

template <typename ModelField>
struct ObjectFieldToValues<std::optional<ModelField>>
{
    template <typename T, bool JoinedValues>
    static auto set(const std::optional<ModelField>* column, const BindingPayload<T, JoinedValues>& model,
                    std::size_t columnIndex, soci::values& values) -> void
    {
        if (column->has_value())
        {
            ObjectFieldToValues<ModelField>::set(&column->value(), model, columnIndex, values);
            return;
        }

        setOptionalNullValue(values, model.getModelInfo(), columnIndex);
    }
};

template <typename ModelField, typename SociType>
struct ObjectFieldToValuesWithCast
{
    template <typename T, bool JoinedValues>
    static auto set(const ModelField* column, const BindingPayload<T, JoinedValues>& model, std::size_t columnIndex,
                    soci::values& values) -> void
    {
        const auto& fieldName = model.getModelInfo().columnsInfo[columnIndex].name;
        values.set(fieldName, checkedNumericCast<SociType>(*column, fieldName));
    }
};

template <SociConvertableToDouble ModelField>
struct ObjectFieldToValues<ModelField> : ObjectFieldToValuesWithCast<ModelField, double>
{
};

template <SociConvertableToInt ModelField>
struct ObjectFieldToValues<ModelField> : ObjectFieldToValuesWithCast<ModelField, int>
{
};

template <SociConvertableToLongLong ModelField>
struct ObjectFieldToValues<ModelField> : ObjectFieldToValuesWithCast<ModelField, long long>
{
};

template <SociConvertableToUnsignedLongLong ModelField>
struct ObjectFieldToValues<ModelField> : ObjectFieldToValuesWithCast<ModelField, unsigned long long>
{
};
} // namespace orm::db::binding
