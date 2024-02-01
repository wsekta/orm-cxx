#include <format>

#include "BindingPayload.hpp"
#include "orm-cxx/utils/ConstexprFor.hpp"
#include "orm-cxx/utils/DisableExternalsWarning.hpp"
#include "soci/values.h"

namespace orm::db::binding
{
template <typename ModelField>
struct ObjectFieldFromValues
{
    template <typename T>
    static auto get(ModelField* column, const BindingPayload<T>& model, std::size_t columnIndex,
                    const soci::values& values) -> void
    {
        if constexpr (orm::model::checkIfIsModelWithId<ModelField>() == true)
        {
            auto foreignFieldName = model.getModelInfo().columnsInfo[columnIndex].name;
            auto foreignModelAsTuple = rfl::to_view(column).values();
            auto foreignModel = model.getModelInfo().foreignModelsInfo.at(foreignFieldName);
            //            foreignFieldName = std::format("{}.{}", model.getModelInfo().tableName, foreignFieldName);

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
        else
        {
            auto fieldName = std::format("{}_{}", model.getModelInfo().tableName,
                                         model.getModelInfo().columnsInfo[columnIndex].name);
            *column = values.get<ModelField>(fieldName);
        }
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

template <>
struct ObjectFieldFromValues<float> : ObjectFieldFromValuesWithCast<float, double>
{
};

template <>
struct ObjectFieldFromValues<bool> : ObjectFieldFromValuesWithCast<bool, int>
{
};
template <>
struct ObjectFieldFromValues<int8_t> : ObjectFieldFromValuesWithCast<int8_t, int>
{
};

template <>
struct ObjectFieldFromValues<char> : ObjectFieldFromValuesWithCast<char, int>
{
};

template <>
struct ObjectFieldFromValues<unsigned char> : ObjectFieldFromValuesWithCast<unsigned char, int>
{
};

template <>
struct ObjectFieldFromValues<short> : ObjectFieldFromValuesWithCast<short, int>
{
};

template <>
struct ObjectFieldFromValues<unsigned short> : ObjectFieldFromValuesWithCast<unsigned short, int>
{
};

template <>
struct ObjectFieldFromValues<unsigned int> : ObjectFieldFromValuesWithCast<unsigned int, unsigned long long>
{
};

template <>
struct ObjectFieldFromValues<long> : ObjectFieldFromValuesWithCast<long, int>
{
};

template <>
struct ObjectFieldFromValues<unsigned long> : ObjectFieldFromValuesWithCast<unsigned long, unsigned long long>
{
};
} // namespace orm::db::binding
