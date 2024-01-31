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
struct ObjectFieldToValues<bool>
{
    template <typename T>
    static auto set(const bool* column, const BindingPayload<T>& model, std::size_t columnIndex, soci::values& values)
        -> void
    {
        values.set(model.getModelInfo().columnsInfo[columnIndex].name, static_cast<int>(*column));
    }
};

template <>
struct ObjectFieldToValues<int8_t>
{
    template <typename T>
    static auto set(const int8_t* column, const BindingPayload<T>& model, std::size_t columnIndex, soci::values& values)
        -> void
    {
        values.set(model.getModelInfo().columnsInfo[columnIndex].name, static_cast<int>(*column));
    }
};

template <>
struct ObjectFieldToValues<char>
{
    template <typename T>
    static auto set(const char* column, const BindingPayload<T>& model, std::size_t columnIndex, soci::values& values)
        -> void
    {
        values.set(model.getModelInfo().columnsInfo[columnIndex].name, static_cast<int>(*column));
    }
};

template <>
struct ObjectFieldToValues<unsigned char>
{
    template <typename T>
    static auto set(const unsigned char* column, const BindingPayload<T>& model, std::size_t columnIndex,
                    soci::values& values) -> void
    {
        values.set(model.getModelInfo().columnsInfo[columnIndex].name, static_cast<int>(*column));
    }
};

template <>
struct ObjectFieldToValues<short>
{
    template <typename T>
    static auto set(const short* column, const BindingPayload<T>& model, std::size_t columnIndex, soci::values& values)
        -> void
    {
        values.set(model.getModelInfo().columnsInfo[columnIndex].name, static_cast<int>(*column));
    }
};

template <>
struct ObjectFieldToValues<unsigned short>
{
    template <typename T>
    static auto set(const unsigned short* column, const BindingPayload<T>& model, std::size_t columnIndex,
                    soci::values& values) -> void
    {
        values.set(model.getModelInfo().columnsInfo[columnIndex].name, static_cast<int>(*column));
    }
};

template <>
struct ObjectFieldToValues<unsigned int>
{
    template <typename T>
    static auto set(const unsigned int* column, const BindingPayload<T>& model, std::size_t columnIndex,
                    soci::values& values) -> void
    {
        values.set(model.getModelInfo().columnsInfo[columnIndex].name, static_cast<unsigned long long>(*column));
    }
};

template <>
struct ObjectFieldToValues<long>
{
    template <typename T>
    static auto set(const long* column, const BindingPayload<T>& model, std::size_t columnIndex, soci::values& values)
        -> void
    {
        values.set(model.getModelInfo().columnsInfo[columnIndex].name, static_cast<int>(*column));
    }
};

template <>
struct ObjectFieldToValues<unsigned long>
{
    template <typename T>
    static auto set(const unsigned long* column, const BindingPayload<T>& model, std::size_t columnIndex,
                    soci::values& values) -> void
    {
        values.set(model.getModelInfo().columnsInfo[columnIndex].name, static_cast<unsigned long long>(*column));
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
            ObjectFieldToValues<ModelField>::set(&column->value(), model, columnIndex, values);
        }
    }
};
} // namespace orm::db::binding
