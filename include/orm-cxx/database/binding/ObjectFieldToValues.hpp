#include <format>

#include "BindingConcepts.hpp"
#include "BindingPayload.hpp"
#include "orm-cxx/utils/ConstexprFor.hpp"
#include "orm-cxx/utils/DisableExternalsWarning.hpp"
#include "soci/values.h"

namespace orm::db::binding
{
template <typename ModelField>
struct ObjectFieldToValues;

template <SociDefaultSupported ModelField>
struct ObjectFieldToValues<ModelField>
{
    template <typename T>
    static auto set(const ModelField* column, const BindingPayload<T>& model, std::size_t columnIndex,
                    soci::values& values) -> void
    {
        values.set(model.getModelInfo().columnsInfo[columnIndex].name, *column);
    }
};

template <ModelWithId ModelField>
struct ObjectFieldToValues<ModelField>
{
    template <typename T>
    static auto set(const ModelField* column, const BindingPayload<T>& model, std::size_t columnIndex,
                    soci::values& values) -> void
    {
        auto foreignFieldName = model.getModelInfo().columnsInfo[columnIndex].name;
        const auto foreignModelAsTuple = rfl::to_view(*column).values();
        auto foreignModel = model.getModelInfo().foreignModelsInfo.at(foreignFieldName);

        auto setForeignFieldToValue =
            [&foreignModel, &values, &foreignFieldName](auto index, const auto foreignModelColumn)
        {
            if (foreignModel.columnsInfo[index].isPrimaryKey)
            {
                values.set(std::format("{}_{}", foreignFieldName, foreignModel.columnsInfo[index].name),
                           *foreignModelColumn);
            }
        };

        orm::utils::constexpr_for_tuple(foreignModelAsTuple, setForeignFieldToValue);
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

template <SociConvertableToDouble ModelField>
struct ObjectFieldToValues<ModelField> : ObjectFieldToValuesWithCast<ModelField, double>
{
};

template <SociConvertableToInt ModelField>
struct ObjectFieldToValues<ModelField> : ObjectFieldToValuesWithCast<ModelField, int>
{
};

template <SociConvertableToUnsignedLongLong ModelField>
struct ObjectFieldToValues<ModelField> : ObjectFieldToValuesWithCast<ModelField, unsigned long long>
{
};
} // namespace orm::db::binding
