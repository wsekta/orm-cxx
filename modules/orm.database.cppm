module;

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <charconv>
#include <cmath>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>
#include "soci/soci.h"
#include "soci/type-conversion.h"
#include "soci/values.h"

export module orm:database;

import orm.reflection;
import :foundation;
import :model;
import :expressions;
import :dynamic_query;
import :static_plan;
import :sql;
import :config;
import :backend_relations;

// database/binding/BindingPayload.hpp
namespace orm::db::binding
{
namespace detail
{
template <typename T, typename SchemaType, std::size_t FieldIndex>
[[nodiscard]] consteval auto mappedColumnPointer() noexcept -> const model::ColumnView*
{
    constexpr auto descriptor = model::modelView<SchemaType, T>();
    for (const auto& column : descriptor->columns)
    {
        if (column.fieldIndex == FieldIndex)
        {
            return &column;
        }
    }
    return nullptr;
}

template <typename T, typename SchemaType, std::size_t FieldIndex>
inline constexpr auto mappedColumn = mappedColumnPointer<T, SchemaType, FieldIndex>();

template <typename T, typename SchemaType, std::size_t FieldIndex>
[[nodiscard]] constexpr auto columnForField() noexcept -> const model::ColumnView&
{
    static_assert(mappedColumn<T, SchemaType, FieldIndex> != nullptr,
                  "A bound field must have a column descriptor in the selected schema");
    return *mappedColumn<T, SchemaType, FieldIndex>;
}

template <typename T, typename SchemaType>
[[nodiscard]] constexpr auto columnForField(std::size_t fieldIndex) -> const model::ColumnView&
{
    constexpr auto descriptor = model::modelView<SchemaType, T>();
    for (const auto& column : descriptor->columns)
    {
        if (column.fieldIndex == fieldIndex)
        {
            return column;
        }
    }
    throw std::invalid_argument{"A bound field has no column descriptor in the selected schema"};
}
} // namespace detail

template <typename T, typename SchemaType, bool JoinedValues = false>
struct BindingPayload
{
    static_assert(SchemaType::template contains<std::remove_cv_t<T>>, "BindingPayload model must belong to its Schema");

    mutable T value{};
    inline static constexpr bool joinedValues = JoinedValues;

    [[nodiscard]] static constexpr auto modelDescriptor() noexcept -> model::ModelView
    {
        return model::modelView<SchemaType, std::remove_cv_t<T>>();
    }

    template <std::size_t FieldIndex>
    [[nodiscard]] static constexpr auto columnDescriptor() noexcept -> const model::ColumnView&
    {
        return detail::columnForField<std::remove_cv_t<T>, SchemaType, FieldIndex>();
    }

    [[nodiscard]] static constexpr auto columnDescriptor(std::size_t fieldIndex) -> const model::ColumnView&
    {
        return detail::columnForField<std::remove_cv_t<T>, SchemaType>(fieldIndex);
    }
};
} // namespace orm::db::binding

// database/binding/BindingConcepts.hpp
namespace orm::db::binding
{
template <typename ModelField, typename... Types>
concept IsOneOfTypes = (std::is_same_v<ModelField, Types> || ...);

template <typename ModelField>
concept SociConvertableToInt =
    IsOneOfTypes<ModelField, bool, std::int8_t, char, unsigned char, short, unsigned short> or
    (std::is_same_v<ModelField, long> and sizeof(long) <= sizeof(int));

template <typename ModelField>
concept SociConvertableToLongLong = std::is_same_v<ModelField, long> and sizeof(long) > sizeof(int);

template <typename ModelField>
concept SociConvertableToUnsignedLongLong = IsOneOfTypes<ModelField, unsigned int, unsigned long>;

template <typename ModelField>
concept SociConvertableToDouble = IsOneOfTypes<ModelField, float>;

template <typename ModelField>
concept SociDefaultSupported = IsOneOfTypes<ModelField, int, long long, unsigned long long, double, std::string>;

template <typename T>
struct OptionalValue
{
    using Type = T;
};

template <typename T>
struct OptionalValue<std::optional<T>>
{
    using Type = T;
};

template <typename T>
using optional_value_t = typename OptionalValue<std::remove_cv_t<T>>::Type;

template <typename SchemaType, typename ModelField>
concept SchemaModel = SchemaType::template contains<std::remove_cv_t<optional_value_t<ModelField>>>;
} // namespace orm::db::binding

// database/binding/ConversionError.hpp
namespace orm::db::binding
{
/**
 * @brief Internal signal for a result value that cannot be represented by the
 * requested model or projection field without loss.
 */
class ConversionError final : public std::runtime_error
{
public:
    explicit ConversionError(std::string message) : std::runtime_error{std::move(message)} {}
};
} // namespace orm::db::binding

// database/binding/NumericConversion.hpp
namespace orm::db::binding
{
template <typename Result, typename Source>
    requires(std::is_integral_v<Result> and std::is_integral_v<Source> and
             not std::is_same_v<std::remove_cv_t<Result>, bool> and not std::is_same_v<std::remove_cv_t<Source>, bool>)
[[nodiscard]] constexpr auto isIntegralInRange(Source value) noexcept -> bool
{
    using result_t = std::remove_cv_t<Result>;
    using source_t = std::remove_cv_t<Source>;

    if constexpr (std::is_signed_v<result_t> == std::is_signed_v<source_t>)
    {
        if constexpr (std::numeric_limits<result_t>::digits >= std::numeric_limits<source_t>::digits)
        {
            return true;
        }
        else
        {
            return value >= static_cast<source_t>(std::numeric_limits<result_t>::lowest()) and
                   value <= static_cast<source_t>(std::numeric_limits<result_t>::max());
        }
    }
    else if constexpr (std::is_signed_v<source_t>)
    {
        if (value < 0)
        {
            return false;
        }

        if constexpr (std::numeric_limits<result_t>::digits >= std::numeric_limits<source_t>::digits)
        {
            return true;
        }
        else
        {
            using unsigned_source_t = std::make_unsigned_t<source_t>;
            return static_cast<unsigned_source_t>(value) <=
                   static_cast<unsigned_source_t>(std::numeric_limits<result_t>::max());
        }
    }
    else
    {
        if constexpr (std::numeric_limits<result_t>::digits >= std::numeric_limits<source_t>::digits)
        {
            return true;
        }
        else
        {
            return value <= static_cast<source_t>(std::numeric_limits<result_t>::max());
        }
    }
}

[[noreturn]] inline auto throwLossyNumericConversion(std::string_view fieldName) -> void
{
    throw ConversionError{"Cannot convert numeric field without data loss: " + std::string{fieldName}};
}

template <typename Result, typename Source>
    requires(std::is_arithmetic_v<Result> and std::is_arithmetic_v<Source>)
auto checkedNumericCast(Source value, std::string_view fieldName) -> Result
{
    if constexpr (std::is_floating_point_v<Source>)
    {
        if (not std::isfinite(value))
        {
            throwLossyNumericConversion(fieldName);
        }
    }

    if constexpr (std::is_integral_v<Result> and std::is_integral_v<Source>)
    {
        if constexpr (std::is_same_v<std::remove_cv_t<Result>, bool>)
        {
            if (value != 0 and value != 1)
            {
                throwLossyNumericConversion(fieldName);
            }
        }
        else if constexpr (not std::is_same_v<std::remove_cv_t<Source>, bool>)
        {
            if (not isIntegralInRange<Result>(value))
            {
                throwLossyNumericConversion(fieldName);
            }
        }

        return static_cast<Result>(value);
    }
    else if constexpr (std::is_integral_v<Result> and std::is_floating_point_v<Source>)
    {
        const auto numericValue = static_cast<long double>(value);
        const auto upperExclusive = std::ldexp(1.0L, std::numeric_limits<Result>::digits);
        const auto lowerInclusive = std::is_signed_v<Result> ? -upperExclusive : 0.0L;

        if (std::trunc(numericValue) != numericValue or numericValue < lowerInclusive or
            numericValue >= upperExclusive or
            (std::is_same_v<std::remove_cv_t<Result>, bool> and numericValue != 0.0L and numericValue != 1.0L))
        {
            throwLossyNumericConversion(fieldName);
        }

        return static_cast<Result>(value);
    }
    else if constexpr (std::is_floating_point_v<Result> and std::is_integral_v<Source>)
    {
        const auto numericValue = static_cast<long double>(value);

        if (numericValue < static_cast<long double>(std::numeric_limits<Result>::lowest()) or
            numericValue > static_cast<long double>(std::numeric_limits<Result>::max()))
        {
            throwLossyNumericConversion(fieldName);
        }

        if constexpr (not std::is_same_v<std::remove_cv_t<Source>, bool>)
        {
            using unsigned_source_t = std::make_unsigned_t<Source>;
            const auto unsignedValue = static_cast<unsigned_source_t>(value);
            auto magnitude = unsignedValue;

            if constexpr (std::is_signed_v<Source>)
            {
                if (value < 0)
                {
                    magnitude = unsigned_source_t{} - unsignedValue;
                }
            }

            if (magnitude != 0)
            {
                const auto significantBits = std::bit_width(magnitude) - std::countr_zero(magnitude);

                if (significantBits > std::numeric_limits<Result>::digits)
                {
                    throwLossyNumericConversion(fieldName);
                }
            }
        }

        return static_cast<Result>(value);
    }
    else if constexpr (std::is_floating_point_v<Result> and std::is_floating_point_v<Source> and
                       std::numeric_limits<Result>::digits < std::numeric_limits<Source>::digits)
    {
        if (value < static_cast<Source>(std::numeric_limits<Result>::lowest()) or
            value > static_cast<Source>(std::numeric_limits<Result>::max()))
        {
            throwLossyNumericConversion(fieldName);
        }

        const auto converted = static_cast<Result>(value);

        if (static_cast<Source>(converted) != value)
        {
            throwLossyNumericConversion(fieldName);
        }

        return converted;
    }
    else
    {
        return static_cast<Result>(value);
    }
}
} // namespace orm::db::binding

// database/binding/NumericValue.hpp
namespace orm::db::binding
{
template <typename Result>
    requires std::is_arithmetic_v<Result>
auto parseNumericValue(std::string_view value, std::string_view fieldName) -> Result
{
    const auto storedValue = std::string{value};

    try
    {
        if constexpr (std::is_floating_point_v<Result>)
        {
            double parsed{};
            const auto* const end = storedValue.data() + storedValue.size();
            const auto [parsedEnd, error] =
                std::from_chars(storedValue.data(), end, parsed, std::chars_format::general);

            if (error != std::errc{} or parsedEnd != end)
            {
                throw ConversionError{"Cannot hydrate numeric field: " + std::string{fieldName}};
            }

            return checkedNumericCast<Result>(parsed, fieldName);
        }
        else if constexpr (std::is_unsigned_v<Result>)
        {
            std::size_t parsedCharacters{};
            const auto firstNonWhitespace = storedValue.find_first_not_of(" \f\n\r\t\v");

            if (firstNonWhitespace != std::string::npos and storedValue[firstNonWhitespace] == '-')
            {
                throw ConversionError{"Cannot hydrate numeric field: " + std::string{fieldName}};
            }

            const auto parsed = std::stoull(storedValue, &parsedCharacters);

            if (parsedCharacters != storedValue.size())
            {
                throw ConversionError{"Cannot hydrate numeric field: " + std::string{fieldName}};
            }

            return checkedNumericCast<Result>(parsed, fieldName);
        }
        else
        {
            std::size_t parsedCharacters{};
            const auto parsed = std::stoll(storedValue, &parsedCharacters);

            if (parsedCharacters != storedValue.size())
            {
                throw ConversionError{"Cannot hydrate numeric field: " + std::string{fieldName}};
            }

            return checkedNumericCast<Result>(parsed, fieldName);
        }
    }
    catch (const ConversionError&)
    {
        throw;
    }
    catch (const std::exception&)
    {
        throw ConversionError{"Cannot hydrate numeric field: " + std::string{fieldName}};
    }
}

template <typename Result, typename Stored>
    requires(std::is_arithmetic_v<Result> and std::is_arithmetic_v<Stored>)
auto tryGetNumericValue(Result* result, const soci::values& values, const std::string& fieldName) -> bool
{
    try
    {
        *result = checkedNumericCast<Result>(values.get<Stored>(fieldName), fieldName);
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

template <typename Result>
    requires std::is_arithmetic_v<Result>
auto getNumericValue(const soci::values& values, const std::string& fieldName) -> Result
{
    auto convertStoredValue = [&values, &fieldName]<typename Stored>() -> Result
    { return checkedNumericCast<Result>(values.get<Stored>(fieldName), fieldName); };

    try
    {
        switch (values.get_properties(fieldName).get_data_type())
        {
        case soci::dt_integer:
            return convertStoredValue.template operator()<int>();
        case soci::dt_long_long:
            return convertStoredValue.template operator()<long long>();
        case soci::dt_unsigned_long_long:
            return convertStoredValue.template operator()<unsigned long long>();
        case soci::dt_double:
            return convertStoredValue.template operator()<double>();
        case soci::dt_string:
            return parseNumericValue<Result>(values.get<std::string>(fieldName), fieldName);
        default:
            throw ConversionError{"Cannot hydrate numeric field: " + fieldName};
        }
    }
    catch (const ConversionError&)
    {
        throw;
    }
    catch (const std::exception&)
    {
        // Locally assembled soci::values instances have no column properties.
        // Try the canonical SOCI arithmetic holders used by orm-cxx instead.
    }

    auto result = Result{};

    if (tryGetNumericValue<Result, int>(&result, values, fieldName) or
        tryGetNumericValue<Result, long long>(&result, values, fieldName) or
        tryGetNumericValue<Result, unsigned long long>(&result, values, fieldName) or
        tryGetNumericValue<Result, double>(&result, values, fieldName))
    {
        return result;
    }

    try
    {
        return parseNumericValue<Result>(values.get<std::string>(fieldName), fieldName);
    }
    catch (const ConversionError&)
    {
        throw;
    }
    catch (const std::exception&)
    {
        throw ConversionError{"Cannot hydrate numeric field: " + fieldName};
    }
}
} // namespace orm::db::binding

// database/binding/PrimaryKey.hpp
namespace orm::db::binding
{
struct PrimaryKeyLess
{
    auto operator()(const PrimaryKey& left, const PrimaryKey& right) const -> bool
    {
        return std::lexicographical_compare(left.begin(), left.end(), right.begin(), right.end(),
                                            [](const auto& lhs, const auto& rhs) { return (lhs <=> rhs) < 0; });
    }
};



inline auto toQueryValue(const query::QueryValue& value) -> query::QueryValue
{
    return value;
}

template <typename T>
struct IsOptional : std::false_type
{
};

template <typename T>
struct IsOptional<std::optional<T>> : std::true_type
{
};

template <typename T>
inline constexpr bool isOptional = IsOptional<std::remove_cv_t<T>>::value;

template <typename T>
auto toPrimaryKeyValue(const T& value, const std::string& fieldName) -> query::QueryValue
{
    using value_t = std::remove_cv_t<T>;

    if constexpr (isOptional<value_t>)
    {
        if (not value.has_value())
        {
            throw std::invalid_argument{"Primary-key field must not be NULL: " + fieldName};
        }

        return toPrimaryKeyValue(value.value(), fieldName);
    }
    else if constexpr (requires { query::QueryValue{value}; })
    {
        return query::QueryValue{value};
    }
    else
    {
        throw std::invalid_argument{"Unsupported primary-key field type: " + fieldName};
    }
}

template <typename SchemaType, typename T>
auto getPrimaryKey(const T& object) -> PrimaryKey
{
    static_assert(SchemaType::template contains<std::remove_cv_t<T>>,
                  "A primary key can be read only from a model in the selected Schema");
    constexpr auto descriptor = model::modelView<SchemaType, std::remove_cv_t<T>>();
    const auto objectAsTuple = reflection::fieldPointers(object);
    PrimaryKey key;
    key.reserve(descriptor->primaryKeyIndices.size());

    auto appendPrimaryKeyField = [&key]<typename Index>(Index, const auto* field)
    {
        using field_t = std::decay_t<decltype(*field)>;

        if constexpr (orm::is_relation_collection_v<field_t>)
        {
            return;
        }
        else
        {
            constexpr auto* column = detail::mappedColumn<std::remove_cv_t<T>, SchemaType, Index::value>;
            if constexpr (column != nullptr)
            {
                if (column->isPrimaryKey)
                {
                    key.push_back(toPrimaryKeyValue(*field, std::string{column->fieldName}));
                }
            }
        }
    };

    utils::constexpr_for_tuple(objectAsTuple, appendPrimaryKeyField);

    if (key.empty())
    {
        throw std::invalid_argument{"Relation endpoint must define a non-empty primary key"};
    }

    return key;
}

inline auto getPrimaryKeyValue(const soci::values& values, const std::string& name,
                               model::ColumnType type) -> query::QueryValue
{
    if (values.get_indicator(name) == soci::i_null)
    {
        throw std::runtime_error{"Cannot hydrate NULL relation primary key: " + name};
    }

    const auto fromStorage = [type, &name](query::QueryValue::Value value)
    {
        try
        {
            return query::QueryValue::fromStorage(type, std::move(value));
        }
        catch (const std::invalid_argument&)
        {
            throw ConversionError{"Cannot hydrate relation primary key without data loss: " + name};
        }
    };

    switch (type)
    {
    case model::ColumnType::Bool:
    case model::ColumnType::Char:
    case model::ColumnType::UnsignedChar:
    case model::ColumnType::Short:
    case model::ColumnType::UnsignedShort:
    case model::ColumnType::Int:
        return fromStorage(getNumericValue<int>(values, name));
    case model::ColumnType::UnsignedInt:
    case model::ColumnType::UnsignedLongLong:
        return fromStorage(getNumericValue<unsigned long long>(values, name));
    case model::ColumnType::LongLong:
        return fromStorage(getNumericValue<long long>(values, name));
    case model::ColumnType::Float:
    case model::ColumnType::Double:
        return fromStorage(getNumericValue<double>(values, name));
    case model::ColumnType::String:
        return fromStorage(values.get<std::string>(name));
    case model::ColumnType::Uuid:
        break;
    }

    throw std::invalid_argument{"Unsupported relation primary-key column: " + name};
}

inline auto getPrimaryKeyColumns(const model::ModelView& model) -> std::vector<const model::ColumnView*>
{
    std::vector<const model::ColumnView*> columns;
    columns.reserve(model->primaryKeyIndices.size());

    for (const auto columnIndex : model->primaryKeyIndices)
    {
        if (columnIndex >= model->columns.size())
        {
            throw std::invalid_argument{"Primary-key descriptor points outside the model columns"};
        }

        const auto& column = model->columns[columnIndex];
        if (column.kind != model::FieldKind::Scalar)
        {
            throw std::invalid_argument{"Relations with model-valued primary-key fields are not supported"};
        }
        columns.push_back(&column);
    }

    if (columns.empty())
    {
        throw std::invalid_argument{"Relation endpoint must define a non-empty primary key"};
    }

    return columns;
}

inline auto relationOwnerAlias(const model::ColumnView& column) -> std::string
{
    return std::format("__orm_owner_{}", column.name);
}
} // namespace orm::db::binding

// database/binding/ObjectFieldFromValues.hpp
namespace orm::db::binding
{
template <typename T>
struct IsOptionalBoundField : std::false_type
{
};

template <typename T>
struct IsOptionalBoundField<std::optional<T>> : std::true_type
{
    using value_type = T;
};

template <typename ModelField>
auto getScalarFieldValue(const soci::values& values, const std::string& fieldName) -> ModelField
{
    if constexpr (IsOptionalBoundField<ModelField>::value)
    {
        if (values.get_indicator(fieldName) == soci::i_null)
        {
            return std::nullopt;
        }

        using value_type = typename IsOptionalBoundField<ModelField>::value_type;
        return getScalarFieldValue<value_type>(values, fieldName);
    }
    else if constexpr (std::is_arithmetic_v<ModelField>)
    {
        return getNumericValue<ModelField>(values, fieldName);
    }
    else if constexpr (SociDefaultSupported<ModelField>)
    {
        return values.get<ModelField>(fieldName);
    }
    else
    {
        throw ConversionError{"Unsupported related model field type: " + fieldName};
    }
}

namespace detail
{
template <typename Owner>
[[nodiscard]] auto selectedScalarAlias(const model::ModelView& owner, const model::ColumnView& column) -> std::string
{
    return std::format("{}_{}", owner->tableName, column.name);
}

template <bool JoinedValues>
[[nodiscard]] auto selectedRelatedAlias(const model::ModelView& owner, const model::ColumnView& relationColumn,
                                        const model::ColumnView& targetColumn) -> std::string
{
    if constexpr (JoinedValues)
    {
        return std::format("{}_{}", relationColumn.name, targetColumn.name);
    }
    else
    {
        return std::format("{}_{}_{}", owner->tableName, relationColumn.name, targetColumn.name);
    }
}

template <typename Related, typename SchemaType, bool JoinedValues>
auto hydrateRelated(Related& related, const model::ModelView& owner, const model::ColumnView& relationColumn,
                    const soci::values& values) -> void
{
    using related_t = std::remove_cv_t<Related>;
    auto relatedFields = reflection::fieldPointers(related);

    auto hydrate = [&]<typename Index>(Index, auto* field)
    {
        using field_t = std::decay_t<decltype(*field)>;
        if constexpr (orm::is_relation_collection_v<field_t>)
        {
            return;
        }
        else
        {
            constexpr auto* targetColumn = mappedColumn<related_t, SchemaType, Index::value>;
            if constexpr (targetColumn != nullptr)
            {
                if constexpr (JoinedValues)
                {
                    const auto alias = selectedRelatedAlias<true>(owner, relationColumn, *targetColumn);
                    *field = getScalarFieldValue<field_t>(values, alias);
                }
                else if (targetColumn->isPrimaryKey)
                {
                    const auto alias = selectedRelatedAlias<false>(owner, relationColumn, *targetColumn);
                    *field = getScalarFieldValue<field_t>(values, alias);
                }
            }
        }
    };

    utils::constexpr_for_tuple(relatedFields, hydrate);
}

template <typename Related, typename SchemaType, bool JoinedValues>
[[nodiscard]] auto relatedPrimaryKeyIsPresent(const model::ModelView& owner, const model::ColumnView& relationColumn,
                                              const soci::values& values) -> bool
{
    constexpr auto related = model::modelView<SchemaType, Related>();
    bool hasPresentPrimaryKey{};
    bool hasNullPrimaryKey{};

    for (const auto primaryKeyIndex : related->primaryKeyIndices)
    {
        const auto& targetColumn = related->columns[primaryKeyIndex];
        const auto alias = selectedRelatedAlias<JoinedValues>(owner, relationColumn, targetColumn);
        if (values.get_indicator(alias) == soci::i_null)
        {
            hasNullPrimaryKey = true;
        }
        else
        {
            hasPresentPrimaryKey = true;
        }
    }

    if (not hasPresentPrimaryKey)
    {
        return false;
    }
    if (hasNullPrimaryKey)
    {
        throw ConversionError{"Cannot hydrate optional relation with a partially null primary key"};
    }
    return true;
}
} // namespace detail

template <typename ModelField>
struct ObjectFieldFromValues
{
    template <typename T, typename SchemaType, bool JoinedValues>
    static auto get(ModelField* field, const BindingPayload<T, SchemaType, JoinedValues>&,
                    const model::ColumnView& column, const soci::values& values) -> void
    {
        constexpr auto owner = model::modelView<SchemaType, T>();

        if constexpr (SchemaModel<SchemaType, ModelField>)
        {
            using related_t = std::remove_cv_t<optional_value_t<ModelField>>;

            if constexpr (IsOptionalBoundField<ModelField>::value)
            {
                if (not detail::relatedPrimaryKeyIsPresent<related_t, SchemaType, JoinedValues>(owner, column, values))
                {
                    *field = std::nullopt;
                    return;
                }
                detail::hydrateRelated<related_t, SchemaType, JoinedValues>(field->emplace(), owner, column, values);
            }
            else
            {
                detail::hydrateRelated<ModelField, SchemaType, JoinedValues>(*field, owner, column, values);
            }
        }
        else
        {
            const auto alias = detail::selectedScalarAlias<T>(owner, column);
            *field = getScalarFieldValue<ModelField>(values, alias);
        }
    }

    template <typename T, typename SchemaType, bool JoinedValues, std::size_t FieldIndex>
    static auto get(ModelField* field, const BindingPayload<T, SchemaType, JoinedValues>& payload,
                    std::integral_constant<std::size_t, FieldIndex>, const soci::values& values) -> void
    {
        get(field, payload, payload.template columnDescriptor<FieldIndex>(), values);
    }

    template <typename T, typename SchemaType, bool JoinedValues>
    static auto get(ModelField* field, const BindingPayload<T, SchemaType, JoinedValues>& payload,
                    std::size_t fieldIndex, const soci::values& values) -> void
    {
        get(field, payload, payload.columnDescriptor(fieldIndex), values);
    }
};
} // namespace orm::db::binding

// database/binding/NullBinding.hpp
namespace orm::db::binding
{
template <typename T>
inline auto bindTypedNull(soci::values& values, std::string_view name, const T& value) -> void
{
    const auto parameterName = std::string{name};

    // SOCI's initial values::set() conversion resets the indicator to i_ok.
    // Set the indicator only after the named value and its storage type exist.
    values.set(parameterName, value);
    values.set(parameterName, value, soci::i_null);
}
} // namespace orm::db::binding

// database/binding/ObjectFieldToValues.hpp
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
    case model::ColumnType::Uuid:
        throw std::invalid_argument{"Cannot bind NULL value with unsupported column type"};
    }

    throw std::invalid_argument{"Cannot bind NULL value with unsupported column type"};
}

template <typename T>
struct IsOptionalScalarField : std::false_type
{
};

template <typename T>
struct IsOptionalScalarField<std::optional<T>> : std::true_type
{
    using value_type = T;
};

template <typename ModelField>
auto setScalarFieldValue(soci::values& values, const std::string& fieldName, const ModelField& field,
                         model::ColumnType columnType) -> void
{
    if constexpr (IsOptionalScalarField<ModelField>::value)
    {
        if (not field.has_value())
        {
            setNullValue(values, fieldName, columnType);
            return;
        }

        setScalarFieldValue(values, fieldName, field.value(), columnType);
    }
    else if constexpr (SociConvertableToDouble<ModelField>)
    {
        values.set(fieldName, checkedNumericCast<double>(field, fieldName));
    }
    else if constexpr (SociConvertableToInt<ModelField>)
    {
        values.set(fieldName, checkedNumericCast<int>(field, fieldName));
    }
    else if constexpr (SociConvertableToLongLong<ModelField>)
    {
        values.set(fieldName, checkedNumericCast<long long>(field, fieldName));
    }
    else if constexpr (SociConvertableToUnsignedLongLong<ModelField>)
    {
        values.set(fieldName, checkedNumericCast<unsigned long long>(field, fieldName));
    }
    else if constexpr (SociDefaultSupported<ModelField>)
    {
        if constexpr (std::is_arithmetic_v<ModelField>)
        {
            values.set(fieldName, checkedNumericCast<ModelField>(field, fieldName));
        }
        else
        {
            values.set(fieldName, field);
        }
    }
    else
    {
        throw std::invalid_argument{"Unsupported related model field type: " + fieldName};
    }
}

namespace detail
{
template <typename Related, typename SchemaType>
auto serializeRelated(const Related& related, const model::ColumnView& relationColumn, soci::values& values) -> void
{
    using related_t = std::remove_cv_t<Related>;
    const auto relatedFields = reflection::fieldPointers(related);

    auto serialize = [&]<typename Index>(Index, const auto* field)
    {
        using field_t = std::decay_t<decltype(*field)>;
        if constexpr (orm::is_relation_collection_v<field_t>)
        {
            return;
        }
        else
        {
            constexpr auto* targetColumn = mappedColumn<related_t, SchemaType, Index::value>;
            if constexpr (targetColumn != nullptr)
            {
                if (targetColumn->isPrimaryKey)
                {
                    const auto parameterName = std::format("{}_{}", relationColumn.name, targetColumn->name);
                    setScalarFieldValue(values, parameterName, *field, targetColumn->type.value());
                }
            }
        }
    };

    utils::constexpr_for_tuple(relatedFields, serialize);
}

template <typename Related, typename SchemaType>
auto setRelatedNull(soci::values& values, const model::ColumnView& relationColumn) -> void
{
    constexpr auto related = model::modelView<SchemaType, Related>();
    for (const auto primaryKeyIndex : related->primaryKeyIndices)
    {
        const auto& targetColumn = related->columns[primaryKeyIndex];
        setNullValue(values, std::format("{}_{}", relationColumn.name, targetColumn.name), targetColumn.type.value());
    }
}
} // namespace detail

template <typename ModelField>
struct ObjectFieldToValues
{
    template <typename T, typename SchemaType, bool JoinedValues>
    static auto set(const ModelField* field, const BindingPayload<T, SchemaType, JoinedValues>&,
                    const model::ColumnView& column, soci::values& values) -> void
    {
        if (column.isAutoIncrement)
        {
            return;
        }

        if constexpr (SchemaModel<SchemaType, ModelField>)
        {
            using related_t = std::remove_cv_t<optional_value_t<ModelField>>;
            if constexpr (IsOptionalScalarField<ModelField>::value)
            {
                if (field->has_value())
                {
                    detail::serializeRelated<related_t, SchemaType>(field->value(), column, values);
                }
                else
                {
                    detail::setRelatedNull<related_t, SchemaType>(values, column);
                }
            }
            else
            {
                detail::serializeRelated<ModelField, SchemaType>(*field, column, values);
            }
        }
        else
        {
            setScalarFieldValue(values, std::string{column.name}, *field, column.type.value());
        }
    }

    template <typename T, typename SchemaType, bool JoinedValues, std::size_t FieldIndex>
    static auto set(const ModelField* field, const BindingPayload<T, SchemaType, JoinedValues>& payload,
                    std::integral_constant<std::size_t, FieldIndex>, soci::values& values) -> void
    {
        set(field, payload, payload.template columnDescriptor<FieldIndex>(), values);
    }

    template <typename T, typename SchemaType, bool JoinedValues>
    static auto set(const ModelField* field, const BindingPayload<T, SchemaType, JoinedValues>& payload,
                    std::size_t fieldIndex, soci::values& values) -> void
    {
        set(field, payload, payload.columnDescriptor(fieldIndex), values);
    }
};
} // namespace orm::db::binding

// database/binding/Binding.hpp
namespace soci
{
template <typename T, typename SchemaType, bool JoinedValues = false>
using BindingPayload = orm::db::binding::BindingPayload<T, SchemaType, JoinedValues>;

template <typename T, typename SchemaType, bool JoinedValues>
struct type_conversion<BindingPayload<T, SchemaType, JoinedValues>>
{
    using base_type = values;

    [[maybe_unused]] static void from_base(const soci::values& values, indicator /*ind*/,
                                           BindingPayload<T, SchemaType, JoinedValues>& model)
    {
        auto& modelValue = model.value;
        auto modelAsTuple = orm::reflection::fieldPointers(modelValue);

        auto getObjectFromValues = [&model, &values](auto fieldIndex, auto* field)
        {
            using field_t = std::decay_t<decltype(*field)>;

            if constexpr (orm::is_relation_collection_v<field_t>)
            {
                return;
            }
            else
            {
                orm::db::binding::ObjectFieldFromValues<field_t>::get(field, model, fieldIndex, values);
            }
        };

        orm::utils::constexpr_for_tuple(modelAsTuple, getObjectFromValues);
    }

    [[maybe_unused]] static void to_base(const BindingPayload<T, SchemaType, JoinedValues>& model, soci::values& values,
                                         indicator& ind)
    {
        auto& modelValue = model.value;
        auto modelAsTuple = orm::reflection::fieldPointers(modelValue);

        auto setObjectToValues = [&model, &values](auto fieldIndex, const auto* field)
        {
            using field_t = std::decay_t<decltype(*field)>;

            if constexpr (orm::is_relation_collection_v<field_t>)
            {
                return;
            }
            else
            {
                orm::db::binding::ObjectFieldToValues<field_t>::set(field, model, fieldIndex, values);
            }
        };

        orm::utils::constexpr_for_tuple(modelAsTuple, setObjectToValues);

        ind = i_ok;
    }
};
} // namespace soci

// database/binding/BindingInfo.hpp
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

// database/binding/CollectionBinding.hpp
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

namespace soci
{
template <typename Owner, typename Target, typename SchemaType, bool JoinedValues>
struct type_conversion<orm::db::binding::CollectionPayload<Owner, Target, SchemaType, JoinedValues>>
{
    using base_type = values;

    [[maybe_unused]] static void
    from_base(const soci::values& values, indicator ind,
              orm::db::binding::CollectionPayload<Owner, Target, SchemaType, JoinedValues>& payload)
    {
        orm::db::binding::BindingPayload<Target, SchemaType, JoinedValues> targetPayload{};
        type_conversion<orm::db::binding::BindingPayload<Target, SchemaType, JoinedValues>>::from_base(values, ind,
                                                                                                       targetPayload);
        payload.value = std::move(targetPayload.value);

        constexpr auto owner = orm::model::modelView<SchemaType, Owner>();
        const auto ownerColumns = orm::db::binding::getPrimaryKeyColumns(owner);
        payload.ownerKey.clear();
        payload.ownerKey.reserve(ownerColumns.size());

        for (const auto* column : ownerColumns)
        {
            const auto alias = orm::db::binding::relationOwnerAlias(*column);
            payload.ownerKey.push_back(orm::db::binding::getPrimaryKeyValue(values, alias, column->type.value()));
        }
    }

    [[maybe_unused]] static void
    to_base(const orm::db::binding::CollectionPayload<Owner, Target, SchemaType, JoinedValues>& /*payload*/,
            soci::values& /*values*/, indicator& ind)
    {
        ind = i_ok;
    }
};
} // namespace soci

// database/binding/ProjectionBinding.hpp
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

// database/binding/StatementBinding.hpp
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
    case model::ColumnType::Uuid:
        break;
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
    std::visit([&values, &parameterName](const auto& storedValue) { values.set(parameterName, storedValue); },
               value.value.value());
}

inline auto bindStatementParameter(soci::values& values, const StatementParameter& parameter) -> void
{
    bindBoundValue(values, parameter.name, parameter.getBoundValue());
}
} // namespace orm::db::binding

// database.hpp
namespace orm
{
namespace detail
{
inline auto bindStatementParameter(const db::BackendRuntime& runtime, soci::values& values,
                                   const db::StatementParameter& parameter) -> void
{
    runtime.bind(values, parameter.name, parameter.getBoundValue());
}

inline auto bindStatementParameters(const db::BackendRuntime& runtime, soci::values& values,
                                    std::span<const db::StatementParameter> parameters) -> void
{
    for (const auto& parameter : parameters)
    {
        bindStatementParameter(runtime, values, parameter);
    }
}

auto bindModelParameters(const db::BackendRuntime& runtime, soci::values& targetValues,
                         const soci::values& serializedModel, model::ModelView model) -> std::size_t;
auto normalizeAffectedRows(long long affectedRows) -> std::size_t;
[[nodiscard]] auto hasOwningJunction(model::ModelView owner) -> bool;
[[nodiscard]] auto requireCollectionRelation(model::ModelView owner,
                                             std::string_view fieldName) -> const model::RelationView*;
[[nodiscard]] auto requireCollectionTarget(model::ModelView owner, const model::RelationView& relation,
                                           model::TypeId expectedType) -> model::ModelView;
}
export {
 // namespace detail

/**
 * @brief A class representing a database in the ORM framework.
 *
 * This class provides functionality for connecting to a database and executing queries.
 */
class DatabaseCore
{
public:
    /**
     * @brief Constructs a new Database object.
     */
    DatabaseCore();

    /**
     * @brief Constructs a database with an application-supplied backend registry.
     */
    explicit DatabaseCore(db::CommandGeneratorFactory factory);
    DatabaseCore(const DatabaseCore&) = delete;
    DatabaseCore(DatabaseCore&&) = delete;
    auto operator=(const DatabaseCore&) -> DatabaseCore& = delete;
    auto operator=(DatabaseCore&&) -> DatabaseCore& = delete;

    /**
     * @brief Connects to a database.
     *
     * @param connectionString The connection string for the database.
     */
    auto connect(const std::string& connectionString) -> void;

    /**
     * @brief Connects using an explicitly selected registered backend.
     */
    auto connect(db::BackendType requestedBackend, const std::string& connectionString) -> void;

    /**
     * @brief Disconnects from the database.
     */
    auto disconnect() -> void;

    /**
     * @brief Executes a select query and returns the result.
     *
     * @tparam T The type of the query model.
     * @param query The select query of type T to execute.
     * @return The vector of objects of type T returned by the select query.
     */
protected:
    template <typename SchemaType, typename Plan, typename... Args>
    auto selectPlanImpl(const Plan& plan, Args&&... args) -> std::vector<typename Plan::Result>
    {
        Plan::validateShape();
        query::detail::validateParameters<Plan, Args...>();
        if constexpr (query::detail::compiledSqlEligible<Plan>)
        {
            const auto values = std::forward_as_tuple(args...);
            const auto flavor = getBackend().compiledSqlFlavor();
            if constexpr (query::detail::compiledStatement<SchemaType, Plan, db::CompiledSqlFlavor::SQLite>.valid)
            {
                if (flavor == db::CompiledSqlFlavor::SQLite)
                    return executeCompiledPlan<SchemaType, Plan, db::CompiledSqlFlavor::SQLite>(plan, values);
            }
            if constexpr (query::detail::compiledStatement<SchemaType, Plan, db::CompiledSqlFlavor::PostgreSQL>.valid)
            {
                if (flavor == db::CompiledSqlFlavor::PostgreSQL)
                    return executeCompiledPlan<SchemaType, Plan, db::CompiledSqlFlavor::PostgreSQL>(plan, values);
            }
        }
        return executeDynamicPlan<SchemaType>(plan, std::forward<Args>(args)...);
    }
    template <typename SchemaType, typename Plan, typename... Args>
    auto updatePlanImpl(const Plan& plan, Args&&... args) -> std::size_t
    {
        Plan::validateShape();
        query::detail::validateParameters<Plan, Args...>();
        if constexpr (query::detail::compiledSqlEligible<Plan>)
        {
            const auto values = std::forward_as_tuple(args...);
            const auto flavor = getBackend().compiledSqlFlavor();
            if constexpr (query::detail::compiledStatement<SchemaType, Plan, db::CompiledSqlFlavor::SQLite>.valid)
            {
                if (flavor == db::CompiledSqlFlavor::SQLite)
                    return executeCompiledPlan<SchemaType, Plan, db::CompiledSqlFlavor::SQLite>(plan, values);
            }
            if constexpr (query::detail::compiledStatement<SchemaType, Plan, db::CompiledSqlFlavor::PostgreSQL>.valid)
            {
                if (flavor == db::CompiledSqlFlavor::PostgreSQL)
                    return executeCompiledPlan<SchemaType, Plan, db::CompiledSqlFlavor::PostgreSQL>(plan, values);
            }
        }
        return executeDynamicPlan<SchemaType>(plan, std::forward<Args>(args)...);
    }
    template <typename SchemaType, typename Plan, typename... Args>
    auto removePlanImpl(const Plan& plan, Args&&... args) -> std::size_t
    {
        Plan::validateShape();
        query::detail::validateParameters<Plan, Args...>();
        if constexpr (query::detail::compiledSqlEligible<Plan>)
        {
            const auto values = std::forward_as_tuple(args...);
            const auto flavor = getBackend().compiledSqlFlavor();
            if constexpr (query::detail::compiledStatement<SchemaType, Plan, db::CompiledSqlFlavor::SQLite>.valid)
            {
                if (flavor == db::CompiledSqlFlavor::SQLite)
                    return executeCompiledPlan<SchemaType, Plan, db::CompiledSqlFlavor::SQLite>(plan, values);
            }
            if constexpr (query::detail::compiledStatement<SchemaType, Plan, db::CompiledSqlFlavor::PostgreSQL>.valid)
            {
                if (flavor == db::CompiledSqlFlavor::PostgreSQL)
                    return executeCompiledPlan<SchemaType, Plan, db::CompiledSqlFlavor::PostgreSQL>(plan, values);
            }
        }
        return executeDynamicPlan<SchemaType>(plan, std::forward<Args>(args)...);
    }

    template <typename SchemaType, typename Plan, typename... Args>
    auto executeDynamicPlan(const Plan& plan, Args&&... args)
    {
        auto bound = plan.toDynamic(std::forward<Args>(args)...);
        if constexpr (Plan::operation == query::detail::PlanOperation::Select)
            return selectImpl<SchemaType>(bound);
        else if constexpr (Plan::operation == query::detail::PlanOperation::Update)
            return updateImpl<SchemaType>(bound);
        else
            return removeImpl<SchemaType, typename Plan::Model>(query::detail::erase(bound));
    }

    template <typename SchemaType, typename Plan, db::CompiledSqlFlavor Flavor, typename Args>
    auto executeCompiledPlan(const Plan& plan, const Args& values)
    {
        constexpr auto descriptor = model::modelView<SchemaType, typename Plan::Model>();
        constexpr auto& compiled = query::detail::compiledStatement<SchemaType, Plan, Flavor>;
        const auto requirements = compiled.program.view();
        if constexpr (Plan::operation == query::detail::PlanOperation::Select)
        {
            if constexpr (Plan::isProjection)
                detail::validateProjectionAliases<typename Plan::Result>(requirements.projections);
            ensureQuerySupported(descriptor, requirements);
            const auto parameters = query::detail::collectParameters(plan, values);
            return executeSelectStatement<SchemaType, typename Plan::Model, typename Plan::Result, Plan::isProjection>(
                db::StatementView{compiled.view(), parameters}, Plan::shouldJoin,
                [&](auto& rows)
                {
                    (void)rows;
                    if constexpr (!Plan::isProjection)
                        loadIncludedCollections<SchemaType>(descriptor, requirements, rows);
                });
        }
        else
        {
            constexpr auto operation = Plan::operation == query::detail::PlanOperation::Update ? "update" : "remove";
            ensureModelSupported(descriptor, operation);
            if constexpr (Plan::operation == query::detail::PlanOperation::Update)
                requireCapability(getBackendCapabilities().mutations.update, operation, "update is not supported");
            else
                requireCapability(getBackendCapabilities().mutations.remove, operation, "remove is not supported");
            ensurePredicateSupported(std::ranges::any_of(requirements.nodes, [](const auto& node)
                                                         { return node.kind == db::detail::SqlNodeKind::Collection; }),
                                     operation);
            const auto parameters = query::detail::collectParameters(plan, values);
            return executeMutation(db::StatementView{compiled.view(), parameters}, operation);
        }
    }
    template <typename SchemaType, typename T>
    auto selectImpl(Query<T>& query) -> std::vector<T>
    {
        constexpr auto descriptor = model::modelView<SchemaType, T>();
        ensureQuerySupported(descriptor, query.getData());
        const auto statement = getCommandGenerator().select(descriptor, query.getData());
        return executeSelectStatement<SchemaType, T, T, false>(
            db::StatementView{statement.sql, statement.parameters}, query.getData().shouldJoin,
            [&](auto& rows) { loadIncludedCollections<SchemaType>(descriptor, query.getData(), rows); });
    }

    template <typename SchemaType, typename Source, typename Result>
    auto selectImpl(ProjectionQuery<Source, Result>& query) -> std::vector<Result>
    {
        constexpr auto descriptor = model::modelView<SchemaType, Source>();
        ensureQuerySupported(descriptor, query.getData());
        const auto statement = getCommandGenerator().select(descriptor, query.getData());
        return executeSelectStatement<SchemaType, Source, Result, true>(
            db::StatementView{statement.sql, statement.parameters}, query.getData().shouldJoin, [](auto&) {});
    }

    template <typename SchemaType, typename Source, typename Result, bool Projection, typename Loader>
    auto executeSelectStatement(db::StatementView statement, bool shouldJoin,
                                Loader loadIncludes) -> std::vector<Result>
    {
        constexpr auto operation = Projection ? "select projection" : "select";
        ensureStatementWithinBindLimit(statement.parameters.size(), operation);
        std::vector<Result> result;
        try
        {
            soci::values parameterValues;
            detail::bindStatementParameters(getBackend().runtime(), parameterValues, statement.parameters);
            if constexpr (Projection)
            {
                soci::rowset<db::binding::ProjectionPayload<Result>> rows =
                    (sql.prepare << statement.sql, soci::use(parameterValues));
                for (auto& payload : rows)
                    result.push_back(std::move(payload.value));
            }
            else
            {
                auto readRows = [&]<bool Joined>()
                {
                    soci::rowset<db::binding::BindingPayload<Source, SchemaType, Joined>> rows =
                        (sql.prepare << statement.sql, soci::use(parameterValues));
                    for (auto& payload : rows)
                        result.push_back(std::move(payload.value));
                };
                if (shouldJoin)
                    readRows.template operator()<true>();
                else
                    readRows.template operator()<false>();
            }
            loadIncludes(result);
        }
        catch (const db::binding::ConversionError&)
        {
            throw DatabaseError{DatabaseErrorCode::Conversion, backendType, operation,
                                Projection ? "A database result cannot be represented by the requested projection" :
                                             "A database result cannot be represented by the requested model"};
        }
        catch (const soci::soci_error& error)
        {
            throwTranslatedError(error, DatabaseErrorCode::Statement, operation);
        }
        return result;
    }

    /**
     * @brief Executes a insert query for multiple objects.
     *
     * @tparam T The type of the query.
     * @param objects The vector of objects of type T to insert.
     */
    template <typename SchemaType, typename T>
    auto insertImpl(const std::vector<T>& objects) -> void
    {
        for (const auto& object : objects)
        {
            insertImpl<SchemaType>(object);
        }
    }

    /**
     * @brief Executes a insert query for a single object.
     *
     * @tparam T The type of the query.
     * @param object The object of type T to insert.
     */
    template <typename SchemaType, typename T>
    auto insertImpl(T object) -> void
    {
        constexpr auto model = model::modelView<SchemaType, T>();
        ensureModelSupported(model, "insert");
        requireCapability(getBackendCapabilities().mutations.insert, "insert", "insert is not supported");
        const auto command = getCommandGenerator().insert(model);

        const db::binding::BindingPayload<T, SchemaType> payload{};

        payload.value = std::move(object);

        try
        {
            soci::values serializedModel;
            auto indicator = soci::i_ok;
            soci::type_conversion<db::binding::BindingPayload<T, SchemaType>>::to_base(payload, serializedModel,
                                                                                       indicator);

            soci::values parameterValues;
            const auto parameterCount =
                detail::bindModelParameters(getBackend().runtime(), parameterValues, serializedModel, model);
            ensureStatementWithinBindLimit(parameterCount, "insert");

            if (parameterCount == 0)
            {
                sql << command;
            }
            else
            {
                sql << command, soci::use(parameterValues);
            }
        }
        catch (const db::binding::ConversionError&)
        {
            throw DatabaseError{DatabaseErrorCode::Conversion, backendType, "insert",
                                "A model value cannot be represented by the selected backend"};
        }
        catch (const soci::soci_error& error)
        {
            throwTranslatedError(error, DatabaseErrorCode::Statement, "insert");
        }
    }

    /**
     * @brief Executes an update query.
     *
     * @tparam T The type of the query model.
     * @param update The update builder with assignments and a required predicate.
     * @return The number of affected rows.
     */
    template <typename SchemaType, typename T>
    auto updateImpl(const Update<T>& update) -> std::size_t
    {
        constexpr auto model = model::modelView<SchemaType, T>();
        ensureModelSupported(model, "update");
        requireCapability(getBackendCapabilities().mutations.update, "update", "update is not supported");
        if (update.getData().predicate.has_value())
        {
            ensurePredicateSupported(*update.getData().predicate, "update");
        }
        const auto statement = getCommandGenerator().update(model, update.getData());

        return executeMutation(statement, "update");
    }

    /**
     * @brief Executes a delete query for rows matching a predicate.
     *
     * @tparam T The type of the model whose rows will be deleted.
     * @param predicate The required predicate used in the WHERE clause.
     * @return The number of affected rows.
     */
    template <typename SchemaType, typename T>
    auto removeImpl(const query::detail::Predicate& predicate) -> std::size_t
    {
        constexpr auto model = model::modelView<SchemaType, T>();
        ensureModelSupported(model, "remove");
        requireCapability(getBackendCapabilities().mutations.remove, "remove", "remove is not supported");
        ensurePredicateSupported(predicate, "remove");
        const auto statement = getCommandGenerator().remove(model, predicate);

        return executeMutation(statement, "remove");
    }

    /**
     * @brief Execute a create table query for a model.
     *
     * @tparam T The type of the model which table to will be created.
     */
    template <typename SchemaType, typename T>
    auto createTableImpl() -> void
    {
        constexpr auto model = model::modelView<SchemaType, T>();
        ensureModelSupported(model, "create table");
        requireCapability(getBackendCapabilities().schema.createTableIfNotExists, "create table",
                          "idempotent table creation is not supported");
        const auto command = getCommandGenerator().createTable(model);
        executeSql(command, "create table");
    }

    /**
     * @brief Execute a delete table query for a model.
     *
     * @tparam T The type of the model which table to will be deleted.
     */
    template <typename SchemaType, typename T>
    auto deleteTableImpl() -> void
    {
        constexpr auto model = model::modelView<SchemaType, T>();
        ensureModelSupported(model, "drop table");
        requireCapability(getBackendCapabilities().schema.dropTableIfExists, "drop table",
                          "idempotent table removal is not supported");
        const auto command = getCommandGenerator().dropTable(model);
        executeSql(command, "drop table");
    }

    /**
     * @brief Creates junction tables owned by a model's ManyToMany mappings.
     *
     * Endpoint tables must already exist. Inverse mappings intentionally do
     * not create the shared junction table.
     */
    template <typename SchemaType, typename T>
    auto createRelationTablesImpl() -> void
    {
        const auto owner = model::modelView<SchemaType, T>();
        const auto ownsJunctionTable = detail::hasOwningJunction(owner);

        if (not ownsJunctionTable)
        {
            return;
        }

        ensureModelSupported(owner, "create relation tables");
        ensureRelationTableEndpointsExist(owner);

        for (const auto& command : db::relations::createTableStatements(getBackend().dialect(), owner))
        {
            executeSql(command, "create relation table");
        }
    }

    /**
     * @brief Drops junction tables owned by a model's ManyToMany mappings.
     */
    template <typename SchemaType, typename T>
    auto deleteRelationTablesImpl() -> void
    {
        const auto owner = model::modelView<SchemaType, T>();
        const auto ownsJunctionTable = detail::hasOwningJunction(owner);

        if (not ownsJunctionTable)
        {
            return;
        }

        ensureModelSupported(owner, "drop relation tables");
        requireCapability(getBackendCapabilities().relations.junctionTables, "drop relation tables",
                          "junction tables are not supported");
        requireCapability(getBackendCapabilities().schema.dropTableIfExists, "drop relation tables",
                          "idempotent table removal is not supported");

        for (const auto& command : db::relations::dropTableStatements(getBackend().dialect(), owner))
        {
            executeSql(command, "drop relation table");
        }
    }

    /**
     * @brief Creates or changes a OneToMany/ManyToMany association.
     * @return 1 when database state changed, otherwise 0.
     */
    template <typename SchemaType, typename Owner, typename Target>
    auto linkImpl(const Owner& owner, std::string_view relationField, const Target& target) -> std::size_t
    {
        constexpr auto ownerDescriptor = model::modelView<SchemaType, Owner>();
        const auto* relation = detail::requireCollectionRelation(ownerDescriptor, relationField);
        const auto targetDescriptor =
            detail::requireCollectionTarget(ownerDescriptor, *relation, model::typeId<Target>());

        ensureModelSupported(ownerDescriptor, "link relation");
        ensureModelSupported(*targetDescriptor, "link relation");
        const auto ownerKey = db::binding::getPrimaryKey<SchemaType>(owner);
        const auto targetKey = db::binding::getPrimaryKey<SchemaType>(target);

        if (relation->kind == model::RelationKind::OneToMany)
        {
            requireCapability(getBackendCapabilities().relations.oneToMany, "link relation",
                              "one-to-many relations are not supported");
        }
        else
        {
            requireCapability(getBackendCapabilities().relations.manyToMany, "link relation",
                              "many-to-many relations are not supported");
            requireCapability(getBackendCapabilities().mutations.atomicInsertIfAbsent, "link relation",
                              "idempotent relation links are not supported");
        }
        requireCapability(relation->kind == model::RelationKind::OneToMany ? getBackendCapabilities().mutations.update :
                                                                             getBackendCapabilities().mutations.insert,
                          "link relation", "relation mutations are not supported");

        if (not relationEndpointExists(ownerDescriptor, ownerKey) or
            not relationEndpointExists(*targetDescriptor, targetKey))
        {
            throw std::invalid_argument{"Cannot link relation endpoints that do not exist"};
        }

        return executeMutation(
            db::relations::linkStatement(getBackend().dialect(), ownerDescriptor, *relation, ownerKey, targetKey),
            "link relation");
    }

    /**
     * @brief Removes a OneToMany/ManyToMany association.
     * @return 1 when database state changed, otherwise 0.
     */
    template <typename SchemaType, typename Owner, typename Target>
    auto unlinkImpl(const Owner& owner, std::string_view relationField, const Target& target) -> std::size_t
    {
        constexpr auto ownerDescriptor = model::modelView<SchemaType, Owner>();
        const auto* relation = detail::requireCollectionRelation(ownerDescriptor, relationField);
        const auto targetDescriptor =
            detail::requireCollectionTarget(ownerDescriptor, *relation, model::typeId<Target>());

        ensureModelSupported(ownerDescriptor, "unlink relation");
        ensureModelSupported(*targetDescriptor, "unlink relation");
        requireCapability(relation->kind == model::RelationKind::OneToMany ?
                              getBackendCapabilities().relations.oneToMany :
                              getBackendCapabilities().relations.manyToMany,
                          "unlink relation", "the requested collection relation is not supported");
        requireCapability(relation->kind == model::RelationKind::OneToMany ? getBackendCapabilities().mutations.update :
                                                                             getBackendCapabilities().mutations.remove,
                          "unlink relation", "relation mutations are not supported");

        return executeMutation(db::relations::unlinkStatement(getBackend().dialect(), ownerDescriptor, *relation,
                                                              db::binding::getPrimaryKey<SchemaType>(owner),
                                                              db::binding::getPrimaryKey<SchemaType>(target)),
                               "unlink relation");
    }

public:
    /**
     * @brief Get the backend type of the database.
     *
     * @return The backend type of the database.
     */
    [[nodiscard]] auto getBackendType() const noexcept -> db::BackendType;

    /**
     * @brief Returns whether a backend session is currently open.
     */
    [[nodiscard]] auto isConnected() const noexcept -> bool;

    /**
     * @brief Returns the capabilities advertised by the connected backend.
     * @throws DatabaseError when no backend is connected.
     */
    [[nodiscard]] auto getBackendCapabilities() const -> const db::BackendCapabilities&;

    /**
     * @brief Starts a transaction.
     */
    auto beginTransaction() -> void;

    /**
     * @brief Commits a transaction.
     */
    auto commitTransaction() -> void;

    /**
     * @brief Rollbacks a transaction.
     */
    auto rollbackTransaction() -> void;

private:
    template <typename SchemaType, typename Owner, typename Target, bool JoinedValues>
    auto appendCollectionRows(const db::Statement& statement,
                              std::map<db::binding::PrimaryKey, std::vector<Target>, db::binding::PrimaryKeyLess>& groupedTargets) -> void
    {
        ensureStatementWithinBindLimit(statement.parameters.size(), "include collection");
        soci::values parameterValues;
        detail::bindStatementParameters(getBackend().runtime(), parameterValues, statement.parameters);
        soci::rowset<db::binding::CollectionPayload<Owner, Target, SchemaType, JoinedValues>> preparedRowSet =
            (sql.prepare << statement.sql, soci::use(parameterValues));

        for (auto& payload : preparedRowSet)
        {
            groupedTargets[payload.ownerKey].push_back(std::move(payload.value));
        }
    }

    template <typename SchemaType, typename Owner, typename Options>
    auto loadIncludedCollections(model::ModelView ownerDescriptor, const Options& queryData,
                                 std::vector<Owner>& owners) -> void
    {
        if (queryData.includes.empty())
        {
            return;
        }

        for (const auto& includedRelation : queryData.includes)
        {
            (void)detail::requireCollectionRelation(ownerDescriptor, includedRelation);
        }

        if (owners.empty())
        {
            return;
        }

        constexpr auto reflectedFields = reflection::fields<Owner>();
        auto ownerFields = reflection::fieldPointers(owners.front());
        auto loadField = [this, &queryData, &owners, ownerDescriptor, &reflectedFields](auto fieldIndex, auto* field)
        {
            (void)this;
            using collection_t = std::decay_t<decltype(*field)>;

            if constexpr (orm::is_relation_collection_v<collection_t>)
            {
                const auto relationName = std::string{reflectedFields[fieldIndex].name};

                if (std::ranges::find(queryData.includes, relationName) == queryData.includes.end())
                {
                    return;
                }

                const auto* relation = ownerDescriptor.findRelation(relationName);
                assert(relation != nullptr);

                loadCollectionField<SchemaType, decltype(fieldIndex)::value, Owner, collection_t>(
                    ownerDescriptor, queryData, owners, *relation);
            }
        };

        utils::constexpr_for_tuple(ownerFields, loadField);
    }

    template <typename SchemaType, std::size_t FieldIndex, typename Owner, typename Collection, typename Options>
    auto loadCollectionField(model::ModelView ownerDescriptor, const Options& queryData, std::vector<Owner>& owners,
                             const model::RelationView& relation) -> void
    {
        using target_t = orm::relation_target_t<Collection>;

        const auto targetDescriptor =
            detail::requireCollectionTarget(ownerDescriptor, relation, model::typeId<target_t>());

        const auto ownerPrimaryKey = db::binding::getPrimaryKeyColumns(ownerDescriptor);
        const auto runtimeLimits = getBackendRuntimeLimits();
        const auto parameterBudget = runtimeLimits.maxBindParameters.value_or(std::numeric_limits<std::size_t>::max());

        if (parameterBudget < ownerPrimaryKey.size())
        {
            throw DatabaseError{DatabaseErrorCode::UnsupportedFeature, backendType, "include collection",
                                "The backend bind-parameter limit is too small for the relation primary key"};
        }

        const auto batchSize = runtimeLimits.maxBindParameters.has_value() ?
                                   std::max<std::size_t>(1, parameterBudget / ownerPrimaryKey.size()) :
                                   owners.size();
        std::map<db::binding::PrimaryKey, std::vector<target_t>, db::binding::PrimaryKeyLess> groupedTargets;

        orm::Query<target_t> targetQuery;

        if (not queryData.shouldJoin)
        {
            targetQuery.disableJoining();
        }

        const auto baseTargetStatement = getCommandGenerator().select(*targetDescriptor, targetQuery.getData());

        assert(baseTargetStatement.parameters.empty());

        for (std::size_t batchStart = 0; batchStart < owners.size(); batchStart += batchSize)
        {
            const auto batchEnd = std::min(owners.size(), batchStart + batchSize);
            std::vector<db::binding::PrimaryKey> ownerKeys;
            ownerKeys.reserve(batchEnd - batchStart);

            for (auto ownerIndex = batchStart; ownerIndex < batchEnd; ++ownerIndex)
            {
                ownerKeys.push_back(db::binding::getPrimaryKey<SchemaType>(owners[ownerIndex]));
            }

            const auto statement =
                db::relations::collectionSelectStatement(getBackend().dialect(), ownerDescriptor, relation,
                                                         baseTargetStatement.sql, ownerKeys, queryData.shouldJoin);
            if (queryData.shouldJoin)
            {
                appendCollectionRows<SchemaType, Owner, target_t, true>(statement, groupedTargets);
            }
            else
            {
                appendCollectionRows<SchemaType, Owner, target_t, false>(statement, groupedTargets);
            }
        }

        for (auto& owner : owners)
        {
            const auto ownerKey = db::binding::getPrimaryKey<SchemaType>(owner);
            auto ownerFields = reflection::fieldPointers(owner);
            auto* collection = std::get<FieldIndex>(ownerFields);
            const auto targets = groupedTargets.find(ownerKey);

            if (targets == groupedTargets.end())
            {
                collection->setLoaded({});
            }
            else
            {
                collection->setLoaded(targets->second);
            }
        }
    }

    auto executeMutation(const db::Statement& statement, std::string_view operation) -> std::size_t;
    auto executeMutation(db::StatementView statement, std::string_view operation) -> std::size_t;
    auto executeSql(std::string_view statement, std::string_view operation) -> void;
    auto relationEndpointExists(model::ModelView model, const db::binding::PrimaryKey& key) -> bool;
    auto tableExists(std::string_view tableName) -> bool;
    auto ensureRelationTableEndpointsExist(model::ModelView owner) -> void;
    [[nodiscard]] auto getBackend() const -> const db::BackendProvider&;
    [[nodiscard]] auto getCommandGenerator() const -> const db::CommandGenerator&;
    [[nodiscard]] auto getBackendRuntimeLimits() -> db::BackendRuntimeLimits;
    auto ensureStatementWithinBindLimit(std::size_t parameterCount, std::string_view operation) -> void;
    auto ensureModelSupported(model::ModelView model, std::string_view operation) const -> void;
    auto ensureQuerySupported(model::ModelView model, const query::detail::SelectSpec& spec) const -> void;
    auto ensureQuerySupported(model::ModelView model, db::detail::SqlQueryView query) const -> void;
    auto ensurePredicateSupported(const query::detail::Predicate& predicate, std::string_view operation) const -> void;
    auto ensurePredicateSupported(bool containsCollection, std::string_view operation) const -> void;
    auto ensureAffectedRowsAvailable(std::string_view operation) const -> void;
    auto requireCapability(bool supported, std::string_view operation, std::string_view message) const -> void;
    [[noreturn]] auto throwTranslatedError(const soci::soci_error& error, DatabaseErrorCode fallback,
                                           std::string_view operation) -> void;

    soci::session sql;
    std::unique_ptr<soci::transaction> transaction;
    bool transactionFailed = false;
    db::BackendType backendType;
    db::CommandGeneratorFactory commandGeneratorFactory;
    const db::BackendProvider* backend = nullptr;
};

/**
 * @brief Database facade bound to one closed compile-time model schema.
 *
 * Backend selection and connection
 * state remain runtime concerns. Every model
 * operation is checked against SchemaType before the implementation is
 *
 * instantiated.
 */
template <typename SchemaType>
class Database final : public DatabaseCore
{
    static_assert(requires { SchemaType::view; }, "Database requires an orm::Schema<...> type");

    template <typename T>
    static consteval auto requireSchemaModel() -> void
    {
        model::requireSchemaModel<SchemaType, T>();
    }

public:
    using DatabaseCore::DatabaseCore;

    template <query::detail::StaticPlanType Plan, typename... Args>
        requires(Plan::operation == query::detail::PlanOperation::Select)
    auto select(const Plan& plan, Args&&... args) -> std::vector<typename Plan::Result>
    {
        requireSchemaModel<typename Plan::Model>();
        return this->template selectPlanImpl<SchemaType>(plan, std::forward<Args>(args)...);
    }
    template <query::detail::StaticPlanType Plan, typename... Args>
        requires(Plan::operation == query::detail::PlanOperation::Update)
    auto update(const Plan& plan, Args&&... args) -> std::size_t
    {
        requireSchemaModel<typename Plan::Model>();
        return this->template updatePlanImpl<SchemaType>(plan, std::forward<Args>(args)...);
    }
    template <query::detail::StaticPlanType Plan, typename... Args>
        requires(Plan::operation == query::detail::PlanOperation::Remove)
    auto remove(const Plan& plan, Args&&... args) -> std::size_t
    {
        requireSchemaModel<typename Plan::Model>();
        return this->template removePlanImpl<SchemaType>(plan, std::forward<Args>(args)...);
    }

    template <typename T>
    using Payload = db::binding::BindingPayload<T, SchemaType>;

    template <typename T>
    using ProjectionPayload = db::binding::ProjectionPayload<T>;

    template <typename T>
    auto select(Query<T>& query) -> std::vector<T>
    {
        requireSchemaModel<T>();
        return this->template selectImpl<SchemaType>(query);
    }

    template <typename Source, typename Result>
    auto select(ProjectionQuery<Source, Result>& query) -> std::vector<Result>
    {
        requireSchemaModel<Source>();
        return this->template selectImpl<SchemaType>(query);
    }

    template <typename T>
    auto insert(const std::vector<T>& objects) -> void
    {
        requireSchemaModel<T>();
        this->template insertImpl<SchemaType>(objects);
    }

    template <typename T>
    auto insert(T object) -> void
    {
        requireSchemaModel<T>();
        this->template insertImpl<SchemaType>(std::move(object));
    }

    template <typename T>
    auto update(const Update<T>& update) -> std::size_t
    {
        requireSchemaModel<T>();
        return this->template updateImpl<SchemaType>(update);
    }

    template <typename T, typename P>
        requires query::detail::PredicateFor<P, T> && query::detail::ORM_QUERY_WRITE_SAFE<P>
    auto remove(const P& predicate) -> std::size_t
    {
        requireSchemaModel<T>();
        return this->template removeImpl<SchemaType, T>(query::detail::erase(predicate));
    }

    template <typename T>
    auto createTable() -> void
    {
        requireSchemaModel<T>();
        this->template createTableImpl<SchemaType, T>();
    }

    template <typename T>
    auto deleteTable() -> void
    {
        requireSchemaModel<T>();
        this->template deleteTableImpl<SchemaType, T>();
    }

    template <typename T>
    auto createRelationTables() -> void
    {
        requireSchemaModel<T>();
        this->template createRelationTablesImpl<SchemaType, T>();
    }

    template <typename T>
    auto deleteRelationTables() -> void
    {
        requireSchemaModel<T>();
        this->template deleteRelationTablesImpl<SchemaType, T>();
    }

    template <auto Member, typename Owner, typename Target>
        requires query::detail::ORM_QUERY_COLLECTION<Member> &&
                     query::detail::ORM_QUERY_MODEL_TYPE<Owner,
                                                         typename query::detail::CollectionTraits<Member>::Model> &&
                     query::detail::ORM_QUERY_MODEL_TYPE<Target,
                                                         typename query::detail::CollectionTraits<Member>::Target>
    auto link(const Owner& owner, const Target& target) -> std::size_t
    {
        requireSchemaModel<Owner>();
        requireSchemaModel<Target>();
        return this->template linkImpl<SchemaType>(owner, query::detail::CollectionTraits<Member>::name(), target);
    }

    template <auto Member, typename Owner, typename Target>
        requires query::detail::ORM_QUERY_COLLECTION<Member> &&
                     query::detail::ORM_QUERY_MODEL_TYPE<Owner,
                                                         typename query::detail::CollectionTraits<Member>::Model> &&
                     query::detail::ORM_QUERY_MODEL_TYPE<Target,
                                                         typename query::detail::CollectionTraits<Member>::Target>
    auto unlink(const Owner& owner, const Target& target) -> std::size_t
    {
        requireSchemaModel<Owner>();
        requireSchemaModel<Target>();
        return this->template unlinkImpl<SchemaType>(owner, query::detail::CollectionTraits<Member>::name(), target);
    }
};

}
} // namespace orm

