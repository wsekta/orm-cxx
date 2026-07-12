#include <format>
#include <stdexcept>

#include "BindingConcepts.hpp"
#include "BindingPayload.hpp"
#include "orm-cxx/relations.hpp"
#include "orm-cxx/utils/ConstexprFor.hpp"
#include "orm-cxx/utils/DisableExternalsWarning.hpp"
#include "soci/values.h"

namespace orm::db::binding
{
template <typename ModelField>
struct ObjectFieldFromValues;

template <SociDefaultSupported ModelField>
struct ObjectFieldFromValues<ModelField>
{
    template <typename T, bool JoinedValues>
    static auto get(ModelField* column, const BindingPayload<T, JoinedValues>& model, std::size_t columnIndex,
                    const soci::values& values) -> void
    {
        auto fieldName =
            std::format("{}_{}", model.getModelInfo().tableName, model.getModelInfo().columnsInfo[columnIndex].name);
        *column = values.get<ModelField>(fieldName);
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

        auto getForeignFieldFromValue =
            [&foreignModel, &values, &foreignFieldName, &model, &foreignColumnIndex](auto /*fieldIndex*/,
                                                                                    auto foreignModelColumn)
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
                    *foreignModelColumn = values.get<field_t>(fieldName);
                }
                else if (columnInfo.isPrimaryKey)
                {
                    auto fieldName = std::format("{}_{}_{}", model.getModelInfo().tableName, foreignFieldName,
                                                 columnInfo.name);
                    *foreignModelColumn = values.get<field_t>(fieldName);
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
                    std::size_t columnIndex,
                    const soci::values& values) -> void
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

                const auto fieldName =
                    JoinedValues
                        ? std::format("{}_{}", columnInfo.name, foreignColumnInfo.name)
                        : std::format("{}_{}_{}", model.getModelInfo().tableName, columnInfo.name,
                                      foreignColumnInfo.name);

                if (values.get_indicator(fieldName) == soci::i_null)
                {
                    hasNullPrimaryKey = true;
                }
                else
                {
                    hasPresentPrimaryKey = true;
                }
            }

            if (not hasPresentPrimaryKey) { *column = std::nullopt; return; }

            if (hasNullPrimaryKey)
            { throw std::runtime_error{"Cannot hydrate optional relation with a partially null primary key"}; }

            ObjectFieldFromValues<ModelField>::get(&column->emplace(), model, columnIndex, values);
        }
        else
        {
            const auto fieldName =
                std::format("{}_{}", model.getModelInfo().tableName, model.getModelInfo().columnsInfo[columnIndex].name);

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
        *column = static_cast<ModelField>(values.get<SociType>(fieldName));
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

template <SociConvertableToUnsignedLongLong ModelField>
struct ObjectFieldFromValues<ModelField> : ObjectFieldFromValuesWithCast<ModelField, unsigned long long>
{
};
} // namespace orm::db::binding
