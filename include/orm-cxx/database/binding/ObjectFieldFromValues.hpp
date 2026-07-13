#pragma once

#include <format>
#include <optional>
#include <stdexcept>
#include <type_traits>

#include "BindingConcepts.hpp"
#include "BindingPayload.hpp"
#include "ConversionError.hpp"
#include "NumericConversion.hpp"
#include "orm-cxx/relations.hpp"
#include "orm-cxx/utils/ConstexprFor.hpp"
#include "orm-cxx/utils/DisableExternalsWarning.hpp"
#include "soci/values.h"

namespace orm::db::binding
{
template <typename ModelField>
struct ObjectFieldFromValues;

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
    else if constexpr (SociConvertableToDouble<ModelField>)
    {
        return checkedNumericCast<ModelField>(values.get<double>(fieldName), fieldName);
    }
    else if constexpr (SociConvertableToInt<ModelField>)
    {
        return checkedNumericCast<ModelField>(values.get<int>(fieldName), fieldName);
    }
    else if constexpr (SociConvertableToLongLong<ModelField>)
    {
        return checkedNumericCast<ModelField>(values.get<long long>(fieldName), fieldName);
    }
    else if constexpr (SociConvertableToUnsignedLongLong<ModelField>)
    {
        return checkedNumericCast<ModelField>(values.get<unsigned long long>(fieldName), fieldName);
    }
    else if constexpr (SociDefaultSupported<ModelField>)
    {
        if constexpr (std::is_arithmetic_v<ModelField>)
        {
            return checkedNumericCast<ModelField>(values.get<ModelField>(fieldName), fieldName);
        }
        else
        {
            return values.get<ModelField>(fieldName);
        }
    }
    else
    {
        throw ConversionError{"Unsupported related model field type: " + fieldName};
    }
}

template <SociDefaultSupported ModelField>
struct ObjectFieldFromValues<ModelField>
{
    template <typename T, bool JoinedValues>
    static auto get(ModelField* column, const BindingPayload<T, JoinedValues>& model, std::size_t columnIndex,
                    const soci::values& values) -> void
    {
        auto fieldName =
            std::format("{}_{}", model.getModelInfo().tableName, model.getModelInfo().columnsInfo[columnIndex].name);
        if constexpr (std::is_arithmetic_v<ModelField>)
        {
            *column = checkedNumericCast<ModelField>(values.get<ModelField>(fieldName), fieldName);
        }
        else
        {
            *column = values.get<ModelField>(fieldName);
        }
    }
};

template <ModelWithId ModelField>
struct ObjectFieldFromValues<ModelField>
{
    template <typename T, bool JoinedValues>
    static auto get(ModelField* column, const BindingPayload<T, JoinedValues>& model, std::size_t columnIndex,
                    const soci::values& values) -> void
    {
        auto foreignFieldName = model.getModelInfo().columnsInfo[columnIndex].name;
        auto foreignModelAsTuple = rfl::to_view(column).values();
        auto foreignModel = model.getModelInfo().foreignModelsInfo.at(foreignFieldName);
        std::size_t foreignColumnIndex = 0;

        auto getForeignFieldFromValue = [&foreignModel, &values, &foreignFieldName, &model,
                                         &foreignColumnIndex](auto /*fieldIndex*/, auto foreignModelColumn)
        {
            using field_t = std::decay_t<decltype(*foreignModelColumn)>;

            if constexpr (orm::is_relation_collection_v<field_t>)
            {
                return;
            }
            else
            {
                const auto& columnInfo = foreignModel.columnsInfo[foreignColumnIndex++];

                if constexpr (JoinedValues)
                {
                    auto fieldName = std::format("{}_{}", foreignFieldName, columnInfo.name);
                    *foreignModelColumn = getScalarFieldValue<field_t>(values, fieldName);
                }
                else if (columnInfo.isPrimaryKey)
                {
                    auto fieldName =
                        std::format("{}_{}_{}", model.getModelInfo().tableName, foreignFieldName, columnInfo.name);
                    *foreignModelColumn = getScalarFieldValue<field_t>(values, fieldName);
                }
            }
        };

        orm::utils::constexpr_for_tuple(foreignModelAsTuple, getForeignFieldFromValue);
    }
};

template <typename ModelField>
struct ObjectFieldFromValues<std::optional<ModelField>>
{
    template <typename T, bool JoinedValues>
    static auto get(std::optional<ModelField>* column, const BindingPayload<T, JoinedValues>& model,
                    std::size_t columnIndex, const soci::values& values) -> void
    {
        if constexpr (ModelWithId<ModelField>)
        {
            const auto& columnInfo = model.getModelInfo().columnsInfo[columnIndex];
            const auto& foreignModel = model.getModelInfo().foreignModelsInfo.at(columnInfo.name);
            bool hasPresentPrimaryKey{};
            bool hasNullPrimaryKey{};

            for (const auto& foreignColumnInfo : foreignModel.columnsInfo)
            {
                if (not foreignColumnInfo.isPrimaryKey)
                {
                    continue;
                }

                const auto fieldName = JoinedValues ? std::format("{}_{}", columnInfo.name, foreignColumnInfo.name) :
                                                      std::format("{}_{}_{}", model.getModelInfo().tableName,
                                                                  columnInfo.name, foreignColumnInfo.name);

                if (values.get_indicator(fieldName) == soci::i_null)
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
                *column = std::nullopt;
                return;
            }

            if (hasNullPrimaryKey)
            {
                throw ConversionError{"Cannot hydrate optional relation with a partially null primary key"};
            }

            ObjectFieldFromValues<ModelField>::get(&column->emplace(), model, columnIndex, values);
        }
        else
        {
            const auto fieldName = std::format("{}_{}", model.getModelInfo().tableName,
                                               model.getModelInfo().columnsInfo[columnIndex].name);

            if (values.get_indicator(fieldName) == soci::i_null)
            {
                *column = std::nullopt;
            }
            else
            {
                ObjectFieldFromValues<ModelField>::get(&column->emplace(), model, columnIndex, values);
            }
        }
    }
};

template <typename ModelField, typename SociType>
struct ObjectFieldFromValuesWithCast
{
    template <typename T, bool JoinedValues>
    static auto get(ModelField* column, const BindingPayload<T, JoinedValues>& model, std::size_t columnIndex,
                    const soci::values& values) -> void
    {
        auto fieldName =
            std::format("{}_{}", model.getModelInfo().tableName, model.getModelInfo().columnsInfo[columnIndex].name);
        *column = checkedNumericCast<ModelField>(values.get<SociType>(fieldName), fieldName);
    }
};

template <SociConvertableToDouble ModelField>
struct ObjectFieldFromValues<ModelField> : ObjectFieldFromValuesWithCast<ModelField, double>
{
};

template <SociConvertableToInt ModelField>
struct ObjectFieldFromValues<ModelField> : ObjectFieldFromValuesWithCast<ModelField, int>
{
};

template <SociConvertableToLongLong ModelField>
struct ObjectFieldFromValues<ModelField> : ObjectFieldFromValuesWithCast<ModelField, long long>
{
};

template <SociConvertableToUnsignedLongLong ModelField>
struct ObjectFieldFromValues<ModelField> : ObjectFieldFromValuesWithCast<ModelField, unsigned long long>
{
};
} // namespace orm::db::binding
