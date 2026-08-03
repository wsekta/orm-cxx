#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>

#include "BindingConcepts.hpp"
#include "ConversionError.hpp"
#include "NumericValue.hpp"
#include "orm-cxx/reflection/Reflection.hpp"
#include "orm-cxx/utils/ConstexprFor.hpp"
#include "soci/type-conversion.h"
#include "soci/values.h"

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
    static auto get(ResultField* /*field*/, const std::string& fieldName, const soci::values& /*values*/) -> void
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
auto tryGetNumericProjectionValue(ResultField* field, const std::string& fieldName, const soci::values& values) -> bool
{
    try
    {
        *field = checkedNumericCast<ResultField>(values.get<StoredField>(fieldName), fieldName);

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
auto getNumericProjectionValue(ResultField* field, const std::string& fieldName, const soci::values& values) -> void
{
    *field = getNumericValue<ResultField>(values, fieldName);
}

template <SociDefaultSupported ResultField>
struct ObjectFieldFromProjectionValues<ResultField>
{
    static auto get(ResultField* field, const std::string& fieldName, const soci::values& values) -> void
    {
        if constexpr (std::is_arithmetic_v<ResultField>)
        {
            getNumericProjectionValue(field, fieldName, values);
        }
        else
        {
            *field = values.get<ResultField>(fieldName);
        }
    }
};

template <typename ResultField>
struct ObjectFieldFromProjectionValues<std::optional<ResultField>>
{
    static auto get(std::optional<ResultField>* field, const std::string& fieldName, const soci::values& values) -> void
    {
        if (values.get_indicator(fieldName) == soci::i_null)
        {
            *field = std::nullopt;
            return;
        }

        ResultField value{};
        ObjectFieldFromProjectionValues<ResultField>::get(&value, fieldName, values);
        *field = std::move(value);
    }
};

template <typename ResultField, typename SociType>
struct ObjectFieldFromProjectionValuesWithCast
{
    static auto get(ResultField* field, const std::string& fieldName, const soci::values& values) -> void
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

namespace soci
{
template <typename T>
using ProjectionPayload = orm::db::binding::ProjectionPayload<T>;

template <typename T>
struct type_conversion<ProjectionPayload<T>>
{
    using base_type = values;

    [[maybe_unused]] static void from_base(const soci::values& values, indicator /*ind*/, ProjectionPayload<T>& payload)
    {
        auto resultAsTuple = orm::reflection::fieldPointers(payload.value);
        constexpr auto fields = orm::reflection::fields<T>();

        auto getObjectFromValues = [&fields, &values](auto index, auto* field)
        {
            using field_t = std::decay_t<decltype(*field)>;
            orm::db::binding::ObjectFieldFromProjectionValues<field_t>::get(field, std::string{fields[index].name},
                                                                            values);
        };

        orm::utils::constexpr_for_tuple(resultAsTuple, getObjectFromValues);
    }

    [[maybe_unused]] static void to_base(const ProjectionPayload<T>& /*payload*/, soci::values& /*values*/,
                                         indicator& ind)
    {
        ind = i_ok;
    }
};
} // namespace soci
