module;

#include <exception>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

export module orm:database_result_binding;

import orm.reflection;
import :foundation;
import :model;
import :expressions;
import :sql_contracts;
export import :database_object_binding;
export import :database_result_storage;

namespace orm::db::binding
{
/**
 * @brief Binding info.
 *
 * This struct is used to store information about a binding how to treat data of a specific type.
 */
struct BindingInfo
{
    bool joinedValues = false;
};
} // namespace orm::db::binding
namespace orm::db::binding
{
template <typename Owner, typename Target, typename SchemaType, bool JoinedValues>
struct CollectionPayload
{
    static_assert(SchemaType::template contains<Owner> && SchemaType::template contains<Target>,
                  "Collection binding endpoints must belong to the selected Schema");
    mutable Target value;
    PrimaryKey ownerKey;
};
} // namespace orm::db::binding

namespace orm::db::binding
{
template <typename T>
struct ProjectionPayload
{
    mutable T value;
};

template <typename ResultField>
struct ObjectFieldFromProjectionValues
{
    static auto get(ResultField* /*field*/, const std::string& fieldName, const auto& /*values*/) -> void
    {
        throw ConversionError{"Unsupported projection result field type: " + fieldName};
    }
};

template <typename ResultField>
auto parseNumericProjectionValue(const std::string& value, const std::string& fieldName) -> ResultField
{
    return parseNumericValue<ResultField>(value, fieldName);
}

template <typename ResultField, typename StoredField>
auto tryGetNumericProjectionValue(ResultField* field, const std::string& fieldName, const auto& values) -> bool
{
    try
    {
        *field = checkedNumericCast<ResultField>(values.template get<StoredField>(fieldName), fieldName);

        return true;
    }
    catch (const ConversionError&)
    {
        throw;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

template <typename ResultField>
auto getNumericProjectionValue(ResultField* field, const std::string& fieldName, const auto& values) -> void
{
    *field = getNumericValue<ResultField>(values, fieldName);
}

template <typename ResultField>
auto readProjectedValue(const auto& values, const std::string& fieldName) -> ResultField
{
    if constexpr (UuidAdaptable<ResultField>)
    {
        return getEncodedScalarValue<ResultField>(values, fieldName);
    }
    else if constexpr (ValueConvertible<ResultField>)
    {
        using canonical_type = typename ValueConverter<ResultField>::Value;
        try
        {
            if constexpr (SociEncodedValue<canonical_type>)
                return ValueConverter<ResultField>::fromValue(getEncodedScalarValue<canonical_type>(values, fieldName));
            else
            {
                canonical_type canonical{};
                ObjectFieldFromProjectionValues<canonical_type>::get(&canonical, fieldName, values);
                return ValueConverter<ResultField>::fromValue(canonical);
            }
        }
        catch (const ConversionError&)
        {
            throw;
        }
        catch (const std::invalid_argument&)
        {
            throw ConversionError{"A database value was rejected by its converter"};
        }
    }
    else
    {
        ResultField value{};
        ObjectFieldFromProjectionValues<ResultField>::get(&value, fieldName, values);
        return value;
    }
}

template <ValueConvertible ResultField>
    requires(!UuidAdaptable<ResultField>)
struct ObjectFieldFromProjectionValues<ResultField>
{
    static auto get(ResultField* field, const std::string& fieldName, const auto& values) -> void
    {
        *field = readProjectedValue<ResultField>(values, fieldName);
    }
};

template <SociEncodedValue ResultField>
struct ObjectFieldFromProjectionValues<ResultField>
{
    static auto get(ResultField* field, const std::string& fieldName, const auto& values) -> void
    {
        *field = getEncodedScalarValue<ResultField>(values, fieldName);
    }
};

template <SociDefaultSupported ResultField>
struct ObjectFieldFromProjectionValues<ResultField>
{
    static auto get(ResultField* field, const std::string& fieldName, const auto& values) -> void
    {
        if constexpr (std::is_arithmetic_v<ResultField>)
        {
            getNumericProjectionValue(field, fieldName, values);
        }
        else
        {
            *field = values.template get<ResultField>(fieldName);
        }
    }
};

template <NullableAdaptable ResultField>
struct ObjectFieldFromProjectionValues<ResultField>
{
    static auto get(ResultField* field, const std::string& fieldName, const auto& values) -> void
    {
        if (values.get_indicator(fieldName) == soci::i_null)
        {
            if constexpr (isOptional<ResultField>)
                field->reset();
            else
                *field = NullableAdapter<ResultField>::none();
            return;
        }

        using contained_type = typename NullableAdapter<ResultField>::Value;
        if constexpr (isOptional<ResultField>)
        {
            auto value = readProjectedValue<contained_type>(values, fieldName);
            if constexpr (std::is_move_constructible_v<contained_type>)
                field->emplace(std::move(value));
            else
                field->emplace(value);
        }
        else
            *field = makeNullableValue<ResultField>(readProjectedValue<contained_type>(values, fieldName));
    }
};

template <typename ResultField, typename SociType>
struct ObjectFieldFromProjectionValuesWithCast
{
    static auto get(ResultField* field, const std::string& fieldName, const auto& values) -> void
    {
        SociType value{};
        getNumericProjectionValue(&value, fieldName, values);
        *field = checkedNumericCast<ResultField>(value, fieldName);
    }
};

template <SociConvertableToDouble ResultField>
struct ObjectFieldFromProjectionValues<ResultField> : ObjectFieldFromProjectionValuesWithCast<ResultField, double>
{
};

template <SociConvertableToInt ResultField>
struct ObjectFieldFromProjectionValues<ResultField> : ObjectFieldFromProjectionValuesWithCast<ResultField, int>
{
};

template <SociConvertableToLongLong ResultField>
struct ObjectFieldFromProjectionValues<ResultField> : ObjectFieldFromProjectionValuesWithCast<ResultField, long long>
{
};

template <SociConvertableToUnsignedLongLong ResultField>
struct ObjectFieldFromProjectionValues<ResultField>
    : ObjectFieldFromProjectionValuesWithCast<ResultField, unsigned long long>
{
};
} // namespace orm::db::binding

namespace orm::db::binding
{
inline auto bindNull(soci::values& values, std::string_view name, model::ColumnType logicalType) -> void
{
    switch (logicalType)
    {
    case model::ColumnType::Bool:
    case model::ColumnType::Char:
    case model::ColumnType::UnsignedChar:
    case model::ColumnType::Short:
    case model::ColumnType::UnsignedShort:
    case model::ColumnType::Int:
        bindTypedNull(values, name, int{});
        return;
    case model::ColumnType::UnsignedInt:
    case model::ColumnType::UnsignedLongLong:
        bindTypedNull(values, name, static_cast<unsigned long long>(0));
        return;
    case model::ColumnType::LongLong:
        bindTypedNull(values, name, static_cast<long long>(0));
        return;
    case model::ColumnType::Float:
    case model::ColumnType::Double:
        bindTypedNull(values, name, 0.0);
        return;
    case model::ColumnType::String:
        bindTypedNull(values, name, std::string{});
        return;
    case model::ColumnType::Date:
    case model::ColumnType::TimeOfDay:
    case model::ColumnType::LocalDateTime:
    case model::ColumnType::UtcTimestamp:
    case model::ColumnType::Uuid:
        bindTypedNull(values, name, std::string{});
        return;
    }

    throw std::invalid_argument{"Cannot bind NULL parameter with unsupported column type"};
}

inline auto hasCompatibleStorage(const BoundValue& value) -> bool
{
    if (value.isNull())
    {
        return true;
    }

    return query::QueryValue::isCompatibleStorage(value.logicalType, value.value.value());
}

inline auto bindBoundValue(soci::values& values, std::string_view name, const BoundValue& value) -> void
{
    if (value.isNull())
    {
        bindNull(values, name, value.logicalType);
        return;
    }

    if (not hasCompatibleStorage(value))
    {
        throw ConversionError{"Bound value storage does not match its logical column type"};
    }

    const auto parameterName = std::string{name};
    std::visit([&values, &parameterName, columnType = value.logicalType](const auto& storedValue)
               { setScalarFieldValue(values, parameterName, storedValue, columnType); }, value.value.value());
}

inline auto bindStatementParameter(soci::values& values, const StatementParameter& parameter) -> void
{
    bindBoundValue(values, parameter.name, parameter.getBoundValue());
}
} // namespace orm::db::binding
