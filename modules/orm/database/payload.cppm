module;

#include <algorithm>
#include <compare>
#include <cstddef>
#include <format>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

export module orm:database_payload;

import orm.reflection;
import :foundation;
import :model;
import :expressions;
import :sql_contracts;
export import :database_numeric;

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
