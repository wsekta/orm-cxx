#pragma once

#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "BindingPayload.hpp"
#include "orm-cxx/query/QueryValue.hpp"
#include "orm-cxx/utils/ConstexprFor.hpp"
#include "orm-cxx/utils/DisableExternalsWarning.hpp"
#include "soci/values.h"

DISABLE_WARNING_PUSH
DISABLE_EXTERNAL_WARNINGS
#include "rfl/to_view.hpp"
DISABLE_WARNING_POP

namespace orm::db::binding
{
using PrimaryKey = std::vector<query::QueryValue::Value>;

inline auto toQueryValue(const query::QueryValue::Value& value) -> query::QueryValue
{
    return std::visit([](const auto& storedValue) { return query::QueryValue{storedValue}; }, value);
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
auto toPrimaryKeyValue(const T& value, const std::string& fieldName) -> query::QueryValue::Value
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
    else if constexpr (std::is_same_v<value_t, long>)
    {
        // Model metadata maps long to ColumnType::Int and SOCI reads it back as int.
        // Keep the in-memory key representation identical to hydrated relation keys.
        return query::QueryValue{static_cast<int>(value)}.get();
    }
    else if constexpr (requires { query::QueryValue{value}; })
    {
        return query::QueryValue{value}.get();
    }
    else
    {
        throw std::invalid_argument{"Unsupported primary-key field type: " + fieldName};
    }
}

template <typename T>
auto getPrimaryKey(const T& object) -> PrimaryKey
{
    const auto& modelInfo = Model<T>::getModelInfo();
    auto& mutableObject = const_cast<T&>(object);
    const auto objectAsTuple = rfl::to_view(mutableObject).values();
    PrimaryKey key;
    key.reserve(modelInfo.idColumnsNames.size());
    std::size_t columnIndex = 0;

    auto appendPrimaryKeyField = [&modelInfo, &key, &columnIndex](auto /*fieldIndex*/, const auto* field)
    {
        using field_t = std::decay_t<decltype(*field)>;

        if constexpr (orm::is_relation_collection_v<field_t>)
        {
            return;
        }
        else
        {
            const auto& columnInfo = modelInfo.columnsInfo[columnIndex++];

            if (columnInfo.isPrimaryKey)
            {
                key.push_back(toPrimaryKeyValue(*field, columnInfo.fieldName));
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
                               model::ColumnType type) -> query::QueryValue::Value
{
    if (values.get_indicator(name) == soci::i_null)
    {
        throw std::runtime_error{"Cannot hydrate NULL relation primary key: " + name};
    }

    switch (type)
    {
    case model::ColumnType::Bool:
    case model::ColumnType::Char:
    case model::ColumnType::UnsignedChar:
    case model::ColumnType::Short:
    case model::ColumnType::UnsignedShort:
    case model::ColumnType::Int:
        return query::QueryValue{values.get<int>(name)}.get();
    case model::ColumnType::UnsignedInt:
    case model::ColumnType::UnsignedLongLong:
        return query::QueryValue{values.get<unsigned long long>(name)}.get();
    case model::ColumnType::LongLong:
        return query::QueryValue{values.get<long long>(name)}.get();
    case model::ColumnType::Float:
    case model::ColumnType::Double:
        return query::QueryValue{values.get<double>(name)}.get();
    case model::ColumnType::String:
        return query::QueryValue{values.get<std::string>(name)}.get();
    case model::ColumnType::Uuid:
    case model::ColumnType::Unknown:
    case model::ColumnType::OneToOne:
        break;
    }

    throw std::invalid_argument{"Unsupported relation primary-key column: " + name};
}

inline auto getPrimaryKeyColumns(const model::ModelInfo& modelInfo) -> std::vector<const model::ColumnInfo*>
{
    std::vector<const model::ColumnInfo*> columns;
    columns.reserve(modelInfo.idColumnsNames.size());

    for (const auto& column : modelInfo.columnsInfo)
    {
        if (column.isPrimaryKey)
        {
            if (column.isForeignModel)
            {
                throw std::invalid_argument{"Relations with model-valued primary-key fields are not supported"};
            }

            columns.push_back(&column);
        }
    }

    if (columns.empty())
    {
        throw std::invalid_argument{"Relation endpoint must define a non-empty primary key"};
    }

    return columns;
}

inline auto relationOwnerAlias(const model::ColumnInfo& column) -> std::string
{
    return std::format("__orm_owner_{}", column.name);
}
} // namespace orm::db::binding
