#include <format>

#include "BindingConcepts.hpp"
#include "BindingPayload.hpp"
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
    template <typename T>
    static auto get(ModelField* column, const BindingPayload<T>& model, std::size_t columnIndex,
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
    template <typename T>
    static auto get(ModelField* column, const BindingPayload<T>& model, std::size_t columnIndex,
                    const soci::values& values) -> void
    {
        auto foreignFieldName = model.getModelInfo().columnsInfo[columnIndex].name;
        auto foreignModelAsTuple = rfl::to_view(column).values();
        auto foreignModel = model.getModelInfo().foreignModelsInfo.at(foreignFieldName);

        auto getForeignFieldFromValue =
            [&foreignModel, &values, &foreignFieldName, &model](auto index, auto foreignModelColumn)
        {
            if (model.bindingInfo.joinedValues)
            {
                auto fieldName = std::format("{}_{}", foreignFieldName, foreignModel.columnsInfo[index].name);
                *foreignModelColumn = values.get<std::decay_t<decltype(*foreignModelColumn)>>(fieldName);
            }
            else if (foreignModel.columnsInfo[index].isPrimaryKey)
            {
                auto fieldName = std::format("{}_{}_{}", model.getModelInfo().tableName, foreignFieldName,
                                             foreignModel.columnsInfo[index].name);
                *foreignModelColumn = values.get<std::decay_t<decltype(*foreignModelColumn)>>(fieldName);
            }
        };

        orm::utils::constexpr_for_tuple(foreignModelAsTuple, getForeignFieldFromValue);
    }
};

template <typename ModelField>
struct ObjectFieldFromValues<std::optional<ModelField>>
{
    template <typename T>
    static auto get(std::optional<ModelField>* column, const BindingPayload<T>& model, std::size_t columnIndex,
                    const soci::values& values) -> void
    {

        auto fieldName =
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
};

template <typename ModelField, typename SociType>
struct ObjectFieldFromValuesWithCast
{
    template <typename T>
    static auto get(ModelField* column, const BindingPayload<T>& model, std::size_t columnIndex,
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
