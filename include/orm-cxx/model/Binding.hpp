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

    static void from_base(values const& /*value*/, indicator /*ind*/, orm::Model<T>& /*model*/)
    {
        // TODO: Implement this
    }

    static void to_base(const orm::Model<T>& model, values& v, indicator& ind)
    {
        auto columns = model.getColumnsInfo();
        auto& object = model.getObject();
        auto view = rfl::to_view(object);

        setColumnTupleGetSize(view.values(), columns, v);
    }

private:
    template <typename Column>
    static void setColumn(std::size_t i, const Column* column, std::vector<orm::model::ColumnInfo>& columnsInfo,
                          values& v)
    {
        auto& columnInfo = columnsInfo[i];

        if (columnInfo.isNotNull)
        {
            v.set(columnInfo.name, *column);
        }
        else
        {
            // TODO: Implement this for optional
            if constexpr (requires(Column t) { t.has_value(); })
            {
                if (column->has_value())
                {
                    v.set(columnInfo.name, column->value());
                }
                else
                {
                    v.set(columnInfo.name, nullptr, i_null);
                }
            }
            else
            {
                throw std::runtime_error(R"(Not optional "NotNull" column)");
            }
        }
    };

    template <typename TupleColumns, std::size_t... Is>
    static void setColumnTupleManual(const TupleColumns& tp, std::index_sequence<Is...>,
                                     std::vector<orm::model::ColumnInfo>& columnsInfo, values& v)
    {
        (setColumn(Is, std::get<Is>(tp), columnsInfo, v), ...);
    }

    template <typename TupleColumns, std::size_t TupSize = std::tuple_size_v<TupleColumns>>
    static void setColumnTupleGetSize(const TupleColumns& tp, std::vector<orm::model::ColumnInfo>& columnsInfo,
                                      values& v)
    {
        setColumnTupleManual(tp, std::make_index_sequence<TupSize>{}, columnsInfo, v);
    }
};
}