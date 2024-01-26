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
    static auto get(ModelField* column, const orm::model::ColumnInfo& columnInfo, const BindingInfo /*bindingInfo*/,
                    const soci::values& values) -> void
    {
        if constexpr (orm::model::checkIfIsModelWithId<ModelField>() == true)
        {
            // values.set(columnInfo.name, column->id);
            std::cout << "get TODO!" << std::endl;
        }
        else
        {
            *column = values.get<ModelField>(columnInfo.name);
        }
    }
};

template <typename ModelField>
struct ObjectFieldFromValues<std::optional<ModelField>>
{
    static auto get(std::optional<ModelField>* column, const orm::model::ColumnInfo& columnInfo,
                    const BindingInfo /*bindingInfo*/, const soci::values& values) -> void
    {
        if (values.get_indicator(columnInfo.name) == soci::i_null)
        {
            *column = std::nullopt;
        }
        else
        {
            *column = values.get<ModelField>(columnInfo.name);
        }
    }
};

template <>
struct ObjectFieldFromValues<float>
{
    static auto get(float* column, const orm::model::ColumnInfo& columnInfo, const BindingInfo /*bindingInfo*/,
                    const soci::values& values) -> void
    {
        *column = static_cast<float>(values.get<double>(columnInfo.name));
    }
};

template <>
struct ObjectFieldFromValues<std::optional<float>>
{
    static auto get(std::optional<float>* column, const orm::model::ColumnInfo& columnInfo,
                    const BindingInfo /*bindingInfo*/, const soci::values& values) -> void
    {
        if (values.get_indicator(columnInfo.name) == soci::i_null)
        {
            *column = std::nullopt;
        }
        else
        {
            *column = static_cast<float>(values.get<double>(columnInfo.name));
        }
    }
};
} // namespace orm::db::binding
