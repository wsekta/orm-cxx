#include <format>

#include "BindingPayload.hpp"
#include "orm-cxx/utils/ConstexprFor.hpp"
#include "orm-cxx/utils/DisableExternalsWarning.hpp"

DISABLE_WARNING_PUSH
DISABLE_EXTERNAL_WARNINGS
#include "soci/values.h"
DISABLE_WARNING_POP

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
                [&foreignModel, &values, &foreignFieldName](auto index, auto foreignModelColumn)
            {
                if (foreignModel.columnsInfo[index].isPrimaryKey)
                {
                    std::cout << std::format("{}_{} == {}", foreignFieldName, foreignModel.columnsInfo[index].name,
                                             values.get<std::decay_t<decltype(*foreignModelColumn)>>(std::format(
                                                 "{}_{}", foreignFieldName, foreignModel.columnsInfo[index].name)))
                              << std::endl;
                    *foreignModelColumn = values.get<std::decay_t<decltype(*foreignModelColumn)>>(
                        std::format("{}_{}", foreignFieldName, foreignModel.columnsInfo[index].name));
                }
            };

            orm::utils::constexpr_for_tuple(foreignModelAsTuple, getForeignFieldFromValue);
        }
        else
        {
            *column = values.get<ModelField>(model.getModelInfo().columnsInfo[columnIndex].name);
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
        if (values.get_indicator(model.getModelInfo().columnsInfo[columnIndex].name) == soci::i_null)
        {
            *column = std::nullopt;
        }
        else
        {
            *column = values.get<ModelField>(model.getModelInfo().columnsInfo[columnIndex].name);
        }
    }
};

template <>
struct ObjectFieldFromValues<float>
{
    template <typename T>
    static auto get(float* column, const BindingPayload<T>& model, std::size_t columnIndex, const soci::values& values)
        -> void
    {
        *column = static_cast<float>(values.get<double>(model.getModelInfo().columnsInfo[columnIndex].name));
    }
};

template <>
struct ObjectFieldFromValues<std::optional<float>>
{
    template <typename T>
    static auto get(std::optional<float>* column, const BindingPayload<T>& model, std::size_t columnIndex,
                    const soci::values& values) -> void
    {
        if (values.get_indicator(model.getModelInfo().columnsInfo[columnIndex].name) == soci::i_null)
        {
            *column = std::nullopt;
        }
        else
        {
            *column = static_cast<float>(values.get<double>(model.getModelInfo().columnsInfo[columnIndex].name));
        }
    }
};
} // namespace orm::db::binding
