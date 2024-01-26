#include "orm-cxx/utils/DisableExternalsWarning.hpp"
#include "BindingPayload.hpp"

DISABLE_WARNING_PUSH
DISABLE_EXTERNAL_WARNINGS
#include "soci/values.h"
DISABLE_WARNING_POP

namespace orm::db::binding
{
template <typename ModelField>
struct ObjectFieldToValues
{
    static auto set(const ModelField* column, const orm::model::ColumnInfo& columnInfo,
                    const BindingInfo /*bindingInfo*/, soci::values& values) -> void
    {
        if constexpr (orm::model::checkIfIsModelWithId<ModelField>() == true)
        {
            // values.set(columnInfo.name, column->id);
            std::cout << "set TODO!" << std::endl;
        }
        else
        {
            values.set(columnInfo.name, *column);
        }
    }
};

template <typename ModelField>
struct ObjectFieldToValues<std::optional<ModelField>>
{
    static auto set(const std::optional<ModelField>* column, const orm::model::ColumnInfo& columnInfo,
                    const BindingInfo /*bindingInfo*/, soci::values& values) -> void
    {
        if (column->has_value())
        {
            values.set(columnInfo.name, column->value());
        }
        else
        {
            values.set(columnInfo.name, decltype(column->value()){}, soci::i_null);
        }
    }
};

template <>
struct ObjectFieldToValues<float>
{
    static auto set(const float* column, const orm::model::ColumnInfo& columnInfo, 
                    const BindingInfo /*bindingInfo*/, soci::values& values) -> void
    {
        values.set(columnInfo.name, static_cast<double>(*column));
    }
};

template <>
struct ObjectFieldToValues<std::optional<float>>
{
    static auto set(const std::optional<float>* column, const orm::model::ColumnInfo& columnInfo,
                    const BindingInfo /*bindingInfo*/, soci::values& values) -> void
    {
        if (column->has_value())
        {
            values.set(columnInfo.name, static_cast<double>(column->value()));
        }
        else
        {
            values.set(columnInfo.name, double{}, soci::i_null);
        }
    }
};
} // namespace orm::db::binding
