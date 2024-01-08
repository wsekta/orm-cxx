#pragma once

#include "orm-cxx/model.hpp"
#include "rfl/to_view.hpp"
#include "soci/type-conversion.h"
#include "soci/values.h"

namespace soci
{
template <typename T>
struct type_conversion<orm::Model<T>>
{
    typedef values base_type;

    static void from_base(const values& v, indicator ind, orm::Model<T>& model)
    {
        if (ind == i_null)
        {
            return;
        }

        auto columns = model.getColumnsInfo();
        auto& modelAsTuple = model.getObject();
        auto view = rfl::to_view(modelAsTuple);

        getObjectFromValues(view.values(), columns, v);
    }

    static void to_base(const orm::Model<T>& model, values& v, indicator& ind)
    {
        auto columns = model.getColumnsInfo();
        auto& modelAsTuple = model.getObject();
        auto view = rfl::to_view(modelAsTuple);

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
            else
            {
                throw std::runtime_error(R"(Not optional "NotNull" column)");
            }
        }
    };

    // getting model from values
    template <typename ModelAsTuple, std::size_t TupleSize = std::tuple_size_v<ModelAsTuple>>
    static void getObjectFromValues(ModelAsTuple& modelAsTuple, const std::vector<orm::model::ColumnInfo>& columnsInfo,
                                    const values& v)
    {
        getObjectFoldIteration(modelAsTuple, std::make_index_sequence<TupleSize>{}, columnsInfo, v);
    }

    template <typename ModelAsTuple, std::size_t... Is>
    static void getObjectFoldIteration(ModelAsTuple& modelAsTuple, std::index_sequence<Is...>,
                                       const std::vector<orm::model::ColumnInfo>& columnsInfo, const values& v)
    {
        (getObjectFieldFromValues(Is, std::get<Is>(modelAsTuple), columnsInfo, v), ...);
    }

    template <typename ModelField>
    static void getObjectFieldFromValues(std::size_t i, ModelField* column,
                                         const std::vector<orm::model::ColumnInfo>& columnsInfo, const values& v)
    {
        auto& columnInfo = columnsInfo[i];

        if (columnInfo.isNotNull)
        {
            *column = v.get<ModelField>(columnInfo.name);
        }
        else
        {
            if constexpr (requires(ModelField t) {
                    t.has_value();
                    t.value();
                })
            {
                if (v.get_indicator(columnInfo.name) == i_null)
                {
                    *column = std::nullopt;
                }
                else
                {
                    *column = v.get<ModelField>(columnInfo.name);
                }
                }
            else
            {
                //throw std::runtime_error(R"(Not optional "NotNull" column)");
            }
        }
    }
};
}