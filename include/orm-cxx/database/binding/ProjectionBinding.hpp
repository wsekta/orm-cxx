#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>

#include "BindingConcepts.hpp"
#include "orm-cxx/utils/ConstexprFor.hpp"
#include "orm-cxx/utils/DisableExternalsWarning.hpp"
#include "soci/type-conversion.h"
#include "soci/values.h"

DISABLE_WARNING_PUSH
DISABLE_EXTERNAL_WARNINGS
#include "rfl/to_view.hpp"
DISABLE_WARNING_POP

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
        throw std::invalid_argument{"Unsupported projection result field type: " + fieldName};
    }
};

template <typename ResultField>
auto parseNumericProjectionValue(const std::string& value) -> ResultField
{
    if constexpr (std::is_floating_point_v<ResultField>)
    {
        return static_cast<ResultField>(std::stod(value));
    }
    else if constexpr (std::is_unsigned_v<ResultField>)
    {
        return static_cast<ResultField>(std::stoull(value));
    }
    else
    {
        return static_cast<ResultField>(std::stoll(value));
    }
}

template <typename ResultField, typename StoredField>
auto tryGetNumericProjectionValue(ResultField* field, const std::string& fieldName, const soci::values& values) -> bool
{
    try
    {
        *field = static_cast<ResultField>(values.get<StoredField>(fieldName));

        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

template <typename ResultField>
auto getNumericProjectionValue(ResultField* field, const std::string& fieldName, const soci::values& values) -> void
{
    if (tryGetNumericProjectionValue<ResultField, ResultField>(field, fieldName, values) or
        tryGetNumericProjectionValue<ResultField, int>(field, fieldName, values) or
        tryGetNumericProjectionValue<ResultField, long long>(field, fieldName, values) or
        tryGetNumericProjectionValue<ResultField, unsigned long long>(field, fieldName, values) or
        tryGetNumericProjectionValue<ResultField, double>(field, fieldName, values))
    {
        return;
    }

    try
    {
        *field = parseNumericProjectionValue<ResultField>(values.get<std::string>(fieldName));
    }
    catch (const std::exception&)
    {
        throw std::invalid_argument{"Cannot hydrate numeric projection result field: " + fieldName};
    }
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
        *field = static_cast<ResultField>(value);
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
        auto resultAsTuple = rfl::to_view(payload.value).values();
        const auto fields = rfl::fields<T>();

        auto getObjectFromValues = [&fields, &values](auto index, auto* field)
        {
            using field_t = std::decay_t<decltype(*field)>;
            orm::db::binding::ObjectFieldFromProjectionValues<field_t>::get(field, std::string{fields[index].name()},
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
