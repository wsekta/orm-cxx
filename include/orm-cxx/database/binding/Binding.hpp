#pragma once

#include "BindingPayload.hpp"
#include "GetObjectFromValues.hpp"
#include "orm-cxx/utils/DisableExternalsWarning.hpp"

DISABLE_WARNING_PUSH

DISABLE_EXTERNAL_WARNINGS

#include "rfl/to_view.hpp"
#include "soci/type-conversion.h"
#include "soci/values.h"

DISABLE_WARNING_POP

namespace soci
{
template <typename T>
using BindingPayload = orm::db::binding::BindingPayload<T>;

template <typename T>
struct type_conversion<BindingPayload<T>>
{
    using base_type = values;

    [[maybe_unused]] static void from_base(const values& v, indicator /*ind*/, BindingPayload<T>& model)
    {
        auto& modelValue = model.value;
        auto modelAsTuple = rfl::to_view(modelValue).values();

        orm::db::binding::getObjectFromValues(modelAsTuple, model, v);
    }

    [[maybe_unused]] static void to_base(const BindingPayload<T>& model, values& v, indicator& ind)
    {
        auto columns = model.getModelInfo().columnsInfo;
        auto& modelValue = model.value;
        auto view = rfl::to_view(modelValue);

        setObjectToValues(view.values(), columns, v);

        ind = i_ok;
    }

private:
    // setting model to values
    template <typename ModelAsTuple, std::size_t TupleSize = std::tuple_size_v<ModelAsTuple>>
    static void setObjectToValues(const ModelAsTuple& modelAsTuple,
                                  const std::vector<orm::model::ColumnInfo>& columnsInfo, values& v)
    {
        setObjectFoldIteration(modelAsTuple, std::make_index_sequence<TupleSize>{}, columnsInfo, v);
    }

    template <typename ModelAsTuple, std::size_t... Is>
    static void setObjectFoldIteration(const ModelAsTuple& modelAsTuple, std::index_sequence<Is...>,
                                       const std::vector<orm::model::ColumnInfo>& columnsInfo, values& v)
    {
        (setObjectFieldToValues(Is, std::get<Is>(modelAsTuple), columnsInfo, v), ...);
    }

    template <typename ModelField>
    static void setObjectFieldToValues(std::size_t i, const ModelField* column,
                                       const std::vector<orm::model::ColumnInfo>& columnsInfo, values& v)
    {
        auto& columnInfo = columnsInfo[i];

        if (columnInfo.isNotNull)
        {

            v.set(columnInfo.name, *column);
        }
        else
        {
            if constexpr (requires(ModelField t) {
                              t.has_value();
                              t.value();
                          })
            {
                if (column->has_value())
                {

                    v.set(columnInfo.name, column->value());
                }
                else
                {

                    v.set(columnInfo.name, decltype(column->value()){}, soci::i_null);
                }
            }
        }
    }

    static void setObjectFieldToValues(std::size_t i, const float* column,
                                       const std::vector<orm::model::ColumnInfo>& columnsInfo, values& v)
    {
        auto& columnInfo = columnsInfo[i];

        v.set(columnInfo.name, static_cast<double>(*column));
    }

    static void setObjectFieldToValues(std::size_t i, const std::optional<float>* column,
                                       const std::vector<orm::model::ColumnInfo>& columnsInfo, values& v)
    {
        auto& columnInfo = columnsInfo[i];

        if (column->has_value())
        {
            v.set(columnInfo.name, static_cast<double>(column->value()));
        }
        else
        {
            v.set(columnInfo.name, double{}, soci::i_null);
        }
    }
};
} // namespace soci
