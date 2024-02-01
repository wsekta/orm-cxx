#include <format>

#include "BindingPayload.hpp"
#include "orm-cxx/utils/ConstexprFor.hpp"
#include "orm-cxx/utils/DisableExternalsWarning.hpp"
#include "soci/values.h"

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

template <typename ModelField, typename SociType>
struct ObjectFieldToValuesWithCast
{
    template <typename T>
    static auto set(const ModelField* column, const BindingPayload<T>& model, std::size_t columnIndex,
                    soci::values& values) -> void
    {
        values.set(model.getModelInfo().columnsInfo[columnIndex].name, static_cast<SociType>(*column));
    }
};

template <>
struct ObjectFieldToValues<float> : ObjectFieldToValuesWithCast<float, double>
{
};

template <>
struct ObjectFieldToValues<bool> : ObjectFieldToValuesWithCast<bool, int>
{
};

template <>
struct ObjectFieldToValues<int8_t> : ObjectFieldToValuesWithCast<int8_t, int>
{
};

template <>
struct ObjectFieldToValues<char> : ObjectFieldToValuesWithCast<char, int>
{
};

template <>
struct ObjectFieldToValues<unsigned char> : ObjectFieldToValuesWithCast<unsigned char, int>
{
};

template <>
struct ObjectFieldToValues<short> : ObjectFieldToValuesWithCast<short, int>
{
};

template <>
struct ObjectFieldToValues<unsigned short> : ObjectFieldToValuesWithCast<unsigned short, int>
{
};

template <>
struct ObjectFieldToValues<unsigned int> : ObjectFieldToValuesWithCast<unsigned int, unsigned long long>
{
};

template <>
struct ObjectFieldToValues<long> : ObjectFieldToValuesWithCast<long, int>
{
};

template <>
struct ObjectFieldToValues<unsigned long> : ObjectFieldToValuesWithCast<unsigned long, unsigned long long>
{
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
            ObjectFieldToValues<ModelField>::set(&column->value(), model, columnIndex, values);
        }
    }
};
} // namespace orm::db::binding
