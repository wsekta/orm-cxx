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

template <>
struct ObjectFieldFromValues<float>
{
    template <typename T>
    static auto get(float* column, const BindingPayload<T>& model, std::size_t columnIndex, const soci::values& values)
        -> void
    {
        auto fieldNames =
            std::format("{}_{}", model.getModelInfo().tableName, model.getModelInfo().columnsInfo[columnIndex].name);
        *column = static_cast<float>(values.get<double>(fieldNames));
    }
};

template <>
struct ObjectFieldFromValues<bool>
{
    template <typename T>
    static auto get(bool* column, const BindingPayload<T>& model, std::size_t columnIndex, const soci::values& values)
        -> void
    {
        auto fieldNames =
            std::format("{}_{}", model.getModelInfo().tableName, model.getModelInfo().columnsInfo[columnIndex].name);
        *column = static_cast<bool>(values.get<int>(fieldNames));
    }
};

template <>
struct ObjectFieldFromValues<char>
{
    template <typename T>
    static auto get(char* column, const BindingPayload<T>& model, std::size_t columnIndex, const soci::values& values)
        -> void
    {
        auto fieldNames =
            std::format("{}_{}", model.getModelInfo().tableName, model.getModelInfo().columnsInfo[columnIndex].name);
        *column = static_cast<char>(values.get<int>(fieldNames));
    }
};

template <>
struct ObjectFieldFromValues<unsigned char>
{
    template <typename T>
    static auto get(unsigned char* column, const BindingPayload<T>& model, std::size_t columnIndex,
                    const soci::values& values) -> void
    {
        auto fieldNames =
            std::format("{}_{}", model.getModelInfo().tableName, model.getModelInfo().columnsInfo[columnIndex].name);
        *column = static_cast<unsigned char>(values.get<int>(fieldNames));
    }
};

template <>
struct ObjectFieldFromValues<short>
{
    template <typename T>
    static auto get(short* column, const BindingPayload<T>& model, std::size_t columnIndex, const soci::values& values)
        -> void
    {
        auto fieldNames =
            std::format("{}_{}", model.getModelInfo().tableName, model.getModelInfo().columnsInfo[columnIndex].name);
        *column = static_cast<short>(values.get<int>(fieldNames));
    }
};

template <>
struct ObjectFieldFromValues<unsigned short>
{
    template <typename T>
    static auto get(unsigned short* column, const BindingPayload<T>& model, std::size_t columnIndex,
                    const soci::values& values) -> void
    {
        auto fieldNames =
            std::format("{}_{}", model.getModelInfo().tableName, model.getModelInfo().columnsInfo[columnIndex].name);
        *column = static_cast<unsigned short>(values.get<int>(fieldNames));
    }
};

template <>
struct ObjectFieldFromValues<unsigned int>
{
    template <typename T>
    static auto get(unsigned int* column, const BindingPayload<T>& model, std::size_t columnIndex,
                    const soci::values& values) -> void
    {
        auto fieldNames =
            std::format("{}_{}", model.getModelInfo().tableName, model.getModelInfo().columnsInfo[columnIndex].name);
        *column = static_cast<unsigned int>(values.get<unsigned long long>(fieldNames));
    }
};

template <>
struct ObjectFieldFromValues<long>
{
    template <typename T>
    static auto get(long* column, const BindingPayload<T>& model, std::size_t columnIndex, const soci::values& values)
        -> void
    {
        auto fieldNames =
            std::format("{}_{}", model.getModelInfo().tableName, model.getModelInfo().columnsInfo[columnIndex].name);
        *column = static_cast<long>(values.get<int>(fieldNames));
    }
};

template <>
struct ObjectFieldFromValues<unsigned long>
{
    template <typename T>
    static auto get(unsigned long* column, const BindingPayload<T>& model, std::size_t columnIndex,
                    const soci::values& values) -> void
    {
        auto fieldNames =
            std::format("{}_{}", model.getModelInfo().tableName, model.getModelInfo().columnsInfo[columnIndex].name);
        *column = static_cast<unsigned long>(values.get<unsigned long long>(fieldNames));
    }
};
} // namespace orm::db::binding
