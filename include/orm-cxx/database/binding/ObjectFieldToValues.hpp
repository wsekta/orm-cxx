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
struct ObjectFieldToValues
{
    template <typename T>
    static auto set(const ModelField* column, const BindingPayload<T>& model, std::size_t columnIndex,
                    soci::values& values) -> void
    {
        if constexpr (orm::model::checkIfIsModelWithId<ModelField>() == true)
        {
            auto foreignFieldName = model.getModelInfo().columnsInfo[columnIndex].name;
            const auto foreignModelAsTuple = rfl::to_view(*column).values();
            auto foreignModel = model.getModelInfo().foreignModelsInfo.at(foreignFieldName);

            auto setForeignFieldToValue =
                [&foreignModel, &values, &foreignFieldName](auto index, const auto foreignModelColumn)
            {
                if (foreignModel.columnsInfo[index].isPrimaryKey)
                {
                    //                    std::cout << std::format("{}_{} == {}", foreignFieldName,
                    //                    foreignModel.columnsInfo[index].name,
                    //                                             *foreignModelColumn)
                    //                              << std::endl;
                    values.set(std::format("{}_{}", foreignFieldName, foreignModel.columnsInfo[index].name),
                               *foreignModelColumn);
                }
            };

            orm::utils::constexpr_for_tuple(foreignModelAsTuple, setForeignFieldToValue);
        }
        else
        {
            values.set(model.getModelInfo().columnsInfo[columnIndex].name, *column);
        }
    }
};

template <typename ModelField>
struct ObjectFieldToValues<std::optional<ModelField>>
{
    template <typename T>
    static auto set(const std::optional<ModelField>* column, const BindingPayload<T>& model, std::size_t columnIndex,
                    soci::values& values) -> void
    {
        if (column->has_value())
        {
            values.set(model.getModelInfo().columnsInfo[columnIndex].name, column->value());
        }
        else
        {
            values.set(model.getModelInfo().columnsInfo[columnIndex].name, decltype(column->value()){}, soci::i_null);
        }
    }
};

template <>
struct ObjectFieldToValues<float>
{
    template <typename T>
    static auto set(const float* column, const BindingPayload<T>& model, std::size_t columnIndex, soci::values& values)
        -> void
    {
        values.set(model.getModelInfo().columnsInfo[columnIndex].name, static_cast<double>(*column));
    }
};

template <>
struct ObjectFieldToValues<std::optional<float>>
{
    template <typename T>
    static auto set(const std::optional<float>* column, const BindingPayload<T>& model, std::size_t columnIndex,
                    soci::values& values) -> void
    {
        if (column->has_value())
        {
            values.set(model.getModelInfo().columnsInfo[columnIndex].name, static_cast<double>(column->value()));
        }
        else
        {
            values.set(model.getModelInfo().columnsInfo[columnIndex].name, double{}, soci::i_null);
        }
    }
};
} // namespace orm::db::binding
