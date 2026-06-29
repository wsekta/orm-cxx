#include <format>
#include <stdexcept>

#include "BindingConcepts.hpp"
#include "BindingPayload.hpp"
#include "orm-cxx/utils/ConstexprFor.hpp"
#include "orm-cxx/utils/DisableExternalsWarning.hpp"
#include "soci/values.h"

namespace orm::db::binding
{
inline auto setNullValue(soci::values& values, const std::string& name, model::ColumnType type) -> void
{
    switch (type)
    {
    case model::ColumnType::Bool:
    case model::ColumnType::Char:
    case model::ColumnType::UnsignedChar:
    case model::ColumnType::Short:
    case model::ColumnType::UnsignedShort:
    case model::ColumnType::Int:
        values.set(name, int{}, soci::i_null);
        return;
    case model::ColumnType::UnsignedInt:
    case model::ColumnType::UnsignedLongLong:
        values.set(name, static_cast<unsigned long long>(0), soci::i_null);
        return;
    case model::ColumnType::LongLong:
        values.set(name, static_cast<long long>(0), soci::i_null);
        return;
    case model::ColumnType::Float:
    case model::ColumnType::Double:
        values.set(name, 0.0, soci::i_null);
        return;
    case model::ColumnType::String:
        values.set(name, std::string{}, soci::i_null);
        return;
    case model::ColumnType::Uuid:
    case model::ColumnType::Unknown:
    case model::ColumnType::OneToOne:
        throw std::invalid_argument{"Cannot bind NULL value with unsupported column type"};
    }

    throw std::invalid_argument{"Cannot bind NULL value with unsupported column type"};
}

template <typename ModelField>
struct ObjectFieldToValues;

template <SociDefaultSupported ModelField>
struct ObjectFieldToValues<ModelField>
{
    template <typename T>
    static auto set(const ModelField* column, const BindingPayload<T>& model, std::size_t columnIndex,
                    soci::values& values) -> void
    {
        const auto& columnInfo = model.getModelInfo().columnsInfo[columnIndex];

        if (columnInfo.isAutoIncrement)
        {
            return;
        }

        values.set(columnInfo.name, *column);
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

            return;
        }

        const auto& columnInfo = model.getModelInfo().columnsInfo[columnIndex];

        if (columnInfo.isForeignModel)
        {
            const auto& foreignModelInfo = model.getModelInfo().foreignModelsInfo.at(columnInfo.name);

            for (const auto& foreignColumnInfo : foreignModelInfo.columnsInfo)
            {
                if (foreignColumnInfo.isPrimaryKey)
                {
                    setNullValue(values, std::format("{}_{}", columnInfo.name, foreignColumnInfo.name),
                                 foreignColumnInfo.type);
                }
            }

            return;
        }

        setNullValue(values, columnInfo.name, columnInfo.type);
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
