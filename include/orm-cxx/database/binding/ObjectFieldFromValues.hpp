#include "BindingPayload.hpp"
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
            // values.set(columnInfo.name, column->id);
            std::cout << "get TODO!" << std::endl;
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
