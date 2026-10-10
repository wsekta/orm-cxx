module;

#include <cstddef>
#include <format>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

export module orm:database_object_binding;

import orm.reflection;
import :foundation;
import :model;
export import :database_payload;

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
