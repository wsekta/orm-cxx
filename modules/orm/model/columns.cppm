module;

#include <array>
#include <concepts>
#include <cstddef>
#include <optional>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

export module orm:model_columns;

import orm.reflection;
import :foundation;
import :model_mapping;
import :model_views;

// model/StaticModel.hpp
namespace orm::model
{
export
{

    template <typename... Models>
    struct Schema;
}
namespace detail
{
template <typename>
struct StaticSchemaTraits;

template <typename... Models>
struct StaticSchemaTraits<Schema<Models...>>
{
    template <typename Model>
    inline static constexpr bool contains = (std::same_as<Model, Models> || ...);

    template <typename Model, std::size_t... Is>
    [[nodiscard]] static consteval auto indexOfImpl(std::index_sequence<Is...>) -> std::size_t
    {
        std::size_t result = 0;
        (void)(((std::same_as<Model, Models>) ? (result = Is, true) : false) || ...);
        return result;
    }

    template <typename Model>
    [[nodiscard]] static consteval auto indexOf() -> std::size_t
    {
        static_assert(contains<Model>, "ORM_SCHEMA_MODEL_MISSING: requested model type does not belong to this schema");
        return indexOfImpl<Model>(std::make_index_sequence<sizeof...(Models)>{});
    }
};

template <typename>
struct IsFixedString : std::false_type
{
};

template <std::size_t Size>
struct IsFixedString<reflection::FixedString<Size>> : std::true_type
{
};

template <typename>
struct IsColumnNames : std::false_type
{
};

template <typename... Definitions>
struct IsColumnNames<ColumnNames<Definitions...>> : std::true_type
{
};

template <typename>
struct IsPrimaryKey : std::false_type
{
};

template <auto... Members>
struct IsPrimaryKey<PrimaryKey<Members...>> : std::true_type
{
};

template <typename>
struct IsAutoIncrement : std::false_type
{
};

template <auto... Members>
struct IsAutoIncrement<AutoIncrement<Members...>> : std::true_type
{
};

template <typename>
struct IsRelations : std::false_type
{
};

template <typename... Descriptors>
struct IsRelations<std::tuple<Descriptors...>>
    : std::bool_constant<(orm::detail::IsRelationDescriptor<Descriptors>::value && ...)>
{
};

template <typename Model>
consteval auto hasValidTableNameDefinition() -> bool
{
    if constexpr (requires { Model::table_name; })
    {
        using definition_t = std::remove_cvref_t<decltype(Model::table_name)>;
        if constexpr (IsFixedString<definition_t>::value)
        {
            return not Model::table_name.view().empty();
        }
        else
        {
            return false;
        }
    }
    else
    {
        return true;
    }
}

template <typename Model>
consteval auto hasValidColumnNamesDefinition() -> bool
{
    if constexpr (requires { Model::columns_names; })
    {
        using definition_t = std::remove_cvref_t<decltype(Model::columns_names)>;
        if constexpr (IsColumnNames<definition_t>::value)
        {
            return definition_t::template isValidFor<Model>();
        }
        else
        {
            return false;
        }
    }
    else
    {
        return true;
    }
}

template <typename Model>
consteval auto hasValidPrimaryKeyDefinition() -> bool
{
    if constexpr (requires { Model::id_columns; })
    {
        using definition_t = std::remove_cvref_t<decltype(Model::id_columns)>;
        if constexpr (IsPrimaryKey<definition_t>::value)
        {
            return definition_t::template isValidFor<Model>();
        }
        else
        {
            return false;
        }
    }
    else
    {
        return true;
    }
}

template <typename Model>
consteval auto hasValidAutoIncrementDefinition() -> bool
{
    if constexpr (requires { Model::auto_increment_columns; })
    {
        using definition_t = std::remove_cvref_t<decltype(Model::auto_increment_columns)>;
        if constexpr (IsAutoIncrement<definition_t>::value)
        {
            return definition_t::template isValidFor<Model>();
        }
        else
        {
            return false;
        }
    }
    else
    {
        return true;
    }
}

template <typename Model>
consteval auto normalizedDefaultTableName()
{
    constexpr auto source = reflection::typeNameStorage<Model>.view();
    constexpr auto prefixSize = source.starts_with("class ")  ? std::string_view{"class "}.size() :
                                source.starts_with("struct ") ? std::string_view{"struct "}.size() :
                                                                0U;
    constexpr auto resultSize = [source]
    {
        std::size_t result{};
        for (std::size_t index = prefixSize; index < source.size(); ++index)
        {
            ++result;
            if (source[index] == ':' && index + 1 < source.size() && source[index + 1] == ':')
            {
                ++index;
            }
        }
        return result;
    }();

    std::array<char, resultSize + 1> result{};
    std::size_t output{};
    for (std::size_t index = prefixSize; index < source.size(); ++index)
    {
        if (source[index] == ':' && index + 1 < source.size() && source[index + 1] == ':')
        {
            result[output++] = '_';
            ++index;
        }
        else
        {
            result[output++] = source[index];
        }
    }
    return result;
}

template <typename Model>
inline constexpr auto defaultTableNameStorage = normalizedDefaultTableName<Model>();

template <typename Model>
consteval auto modelTypeName() -> std::string_view
{
    return reflection::typeNameStorage<Model>.view();
}

template <typename Model>
consteval auto mappedTableName() -> std::string_view
{
    if constexpr (requires { Model::table_name; })
    {
        using definition_t = std::remove_cvref_t<decltype(Model::table_name)>;
        if constexpr (IsFixedString<definition_t>::value)
        {
            return Model::table_name.view();
        }
    }
    constexpr auto& storage = defaultTableNameStorage<Model>;
    return std::string_view{storage.data(), storage.size() - 1};
}

template <typename Model>
consteval auto mappedColumnName(std::string_view fieldName) -> std::string_view
{
    if constexpr (requires { Model::columns_names; })
    {
        using definition_t = std::remove_cvref_t<decltype(Model::columns_names)>;
        if constexpr (IsColumnNames<definition_t>::value && definition_t::template isValidFor<Model>())
        {
            if (const auto mapped = Model::columns_names.find(fieldName); mapped.has_value())
            {
                return mapped.value();
            }
        }
    }
    return fieldName;
}

template <typename Model>
consteval auto isPrimaryKey(std::string_view fieldName) -> bool
{
    if constexpr (requires { Model::id_columns; })
    {
        using definition_t = std::remove_cvref_t<decltype(Model::id_columns)>;
        if constexpr (IsPrimaryKey<definition_t>::value && definition_t::template isValidFor<Model>())
        {
            return Model::id_columns.contains(fieldName);
        }
        return false;
    }
    else
    {
        return fieldName.compare("id") == 0;
    }
}

template <typename Model>
consteval auto isAutoIncrement(std::string_view fieldName) -> bool
{
    if constexpr (requires { Model::auto_increment_columns; })
    {
        using definition_t = std::remove_cvref_t<decltype(Model::auto_increment_columns)>;
        if constexpr (IsAutoIncrement<definition_t>::value && definition_t::template isValidFor<Model>())
        {
            return Model::auto_increment_columns.contains(fieldName);
        }
        return false;
    }
    else
    {
        return false;
    }
}

template <typename Model, std::size_t... Indices>
consteval auto hasDefaultPrimaryKey(std::index_sequence<Indices...>) -> bool
{
    return (((reflectedFieldName<Model, Indices>().compare("id") == 0) &&
             not is_relation_collection_v<reflection::field_type_t<Model, Indices>> &&
             not is_optional_relation_collection_v<reflection::field_type_t<Model, Indices>>) ||
            ...);
}

template <typename Model>
consteval auto hasPrimaryKey() -> bool
{
    if constexpr (requires { Model::id_columns; })
    {
        using definition_t = std::remove_cvref_t<decltype(Model::id_columns)>;
        if constexpr (IsPrimaryKey<definition_t>::value)
        {
            return definition_t::size != 0 && definition_t::template isValidFor<Model>();
        }
        else
        {
            return false;
        }
    }
    else
    {
        return hasDefaultPrimaryKey<Model>(std::make_index_sequence<reflection::fieldCount<Model>>{});
    }
}

template <typename Model, typename SchemaType, std::size_t Index>
consteval auto makeColumn() -> ColumnView
{
    using field_t = reflection::field_type_t<Model, Index>;
    using value_t = std::remove_cv_t<static_optional_value_t<std::remove_cvref_t<field_t>>>;
    constexpr auto fieldName = reflectedFieldName<Model, Index>();
    static_assert(not is_optional_relation_collection_v<field_t>,
                  "ORM_MODEL_OPTIONAL_COLLECTION: relation collections cannot be optional");

    if constexpr (StaticSchemaTraits<SchemaType>::template contains<value_t>)
    {
        static_assert(hasPrimaryKey<value_t>(),
                      "ORM_RELATION_KEYLESS: every to-one target must define a valid primary key");
        return ColumnView{
            .fieldIndex = Index,
            .fieldName = fieldName,
            .name = mappedColumnName<Model>(fieldName),
            .type = std::nullopt,
            .kind = FieldKind::ToOne,
            .targetModelIndex = StaticSchemaTraits<SchemaType>::template indexOf<value_t>(),
            .isPrimaryKey = isPrimaryKey<Model>(fieldName),
            .isAutoIncrement = isAutoIncrement<Model>(fieldName),
            .isNotNull = not isNullable<field_t>,
        };
    }
    else
    {
        return ColumnView{
            .fieldIndex = Index,
            .fieldName = fieldName,
            .name = mappedColumnName<Model>(fieldName),
            .type = logicalType<field_t>(),
            .kind = FieldKind::Scalar,
            .targetModelIndex = noTargetModel,
            .isPrimaryKey = isPrimaryKey<Model>(fieldName),
            .isAutoIncrement = isAutoIncrement<Model>(fieldName),
            .isNotNull = not isNullable<field_t>,
        };
    }
}

template <typename Model, std::size_t OutputIndex, std::size_t... FieldIndices>
consteval auto columnFieldIndex(std::index_sequence<FieldIndices...>) -> std::size_t
{
    constexpr std::array fieldIndices{FieldIndices...};
    constexpr std::array included{
        (not is_relation_collection_v<reflection::field_type_t<Model, FieldIndices>> &&
         not is_optional_relation_collection_v<reflection::field_type_t<Model, FieldIndices>>)...};
    std::size_t output{};
    std::size_t result = reflection::fieldCount<Model>;
    for (std::size_t index = 0; index < included.size(); ++index)
    {
        if (included[index])
        {
            if (output == OutputIndex)
            {
                result = fieldIndices[index];
                break;
            }
            ++output;
        }
    }
    return result;
}

template <typename Model, typename SchemaType, std::size_t... FieldIndices, std::size_t... OutputIndices>
consteval auto makeColumnsDirect(std::index_sequence<FieldIndices...>, std::index_sequence<OutputIndices...>)
{
    return std::array<ColumnView, sizeof...(OutputIndices)>{
        makeColumn<Model, SchemaType,
                   columnFieldIndex<Model, OutputIndices>(std::index_sequence<FieldIndices...>{})>()...};
}

template <typename Model, typename SchemaType, std::size_t... Indices>
consteval auto makeColumns(std::index_sequence<Indices...> fields)
{
    static_assert((not is_optional_relation_collection_v<reflection::field_type_t<Model, Indices>> && ...),
                  "ORM_MODEL_OPTIONAL_COLLECTION: relation collections cannot be optional");
    constexpr auto columnCount =
        ((not is_relation_collection_v<reflection::field_type_t<Model, Indices>> ? 1U : 0U) + ... + 0U);
    return makeColumnsDirect<Model, SchemaType>(fields, std::make_index_sequence<columnCount>{});
}

template <std::size_t Size>
consteval auto hasValidColumnNames(const std::array<ColumnView, Size>& columns) -> bool
{
    bool valid = true;
    for (std::size_t left = 0; left < columns.size() && valid; ++left)
    {
        if (columns[left].fieldName.empty() || columns[left].name.empty())
        {
            valid = false;
            break;
        }
        for (std::size_t right = left + 1; right < columns.size() && valid; ++right)
        {
            if (columns[left].fieldName.compare(columns[right].fieldName) == 0 ||
                columns[left].name.compare(columns[right].name) == 0 ||
                columns[left].fieldName.compare(columns[right].name) == 0 ||
                columns[left].name.compare(columns[right].fieldName) == 0)
            {
                valid = false;
                break;
            }
        }
    }
    return valid;
}

template <std::size_t Size>
consteval auto hasValidPrimaryKey(const std::array<ColumnView, Size>& columns) -> bool
{
    bool valid = true;
    for (const auto& column : columns)
    {
        if (column.isPrimaryKey && (not column.isNotNull || column.kind != FieldKind::Scalar))
        {
            valid = false;
            break;
        }
    }
    return valid;
}

template <std::size_t Size>
consteval auto hasValidAutoIncrement(const std::array<ColumnView, Size>& columns) -> bool
{
    std::size_t primaryKeyCount{};
    std::size_t autoIncrementCount{};
    const ColumnView* autoIncrementColumn{};

    for (const auto& column : columns)
    {
        primaryKeyCount += column.isPrimaryKey ? 1U : 0U;
        if (column.isAutoIncrement)
        {
            ++autoIncrementCount;
            autoIncrementColumn = &column;
        }
    }

    if (autoIncrementCount == 0)
    {
        return true;
    }
    return autoIncrementCount == 1 && primaryKeyCount == 1 && autoIncrementColumn->isPrimaryKey &&
           autoIncrementColumn->isNotNull && autoIncrementColumn->kind == FieldKind::Scalar &&
           autoIncrementColumn->type == ColumnType::Int;
}

template <typename Model>
consteval auto modelPrimaryKeyCount() -> std::size_t
{
    if constexpr (requires { Model::id_columns; })
    {
        using definition_t = std::remove_cvref_t<decltype(Model::id_columns)>;
        if constexpr (IsPrimaryKey<definition_t>::value)
        {
            return definition_t::size;
        }
        else
        {
            return 0;
        }
    }
    else
    {
        return hasDefaultPrimaryKey<Model>(std::make_index_sequence<reflection::fieldCount<Model>>{}) ? 1U : 0U;
    }
}

template <std::size_t ResultSize, std::size_t ColumnCount>
consteval auto makePrimaryKeyIndices(const std::array<ColumnView, ColumnCount>& columns)
{
    std::array<std::size_t, ResultSize> result{};
    std::size_t output{};
    for (std::size_t index = 0; index < columns.size(); ++index)
    {
        if (columns[index].isPrimaryKey)
        {
            result[output++] = index;
        }
    }
    return result;
}

template <typename Model, std::size_t ColumnCount>
consteval auto makeModelPrimaryKeyIndices(const std::array<ColumnView, ColumnCount>& columns)
{
    if constexpr (requires { Model::id_columns; })
    {
        using definition_t = std::remove_cvref_t<decltype(Model::id_columns)>;
        if constexpr (IsPrimaryKey<definition_t>::value && definition_t::template isValidFor<Model>())
        {
            constexpr auto fieldIndices = definition_t::template indices<Model>();
            std::array<std::size_t, fieldIndices.size()> result{};
            for (std::size_t keyIndex = 0; keyIndex < fieldIndices.size(); ++keyIndex)
            {
                bool found{};
                for (std::size_t columnIndex = 0; columnIndex < columns.size(); ++columnIndex)
                {
                    if (columns[columnIndex].fieldIndex == fieldIndices[keyIndex])
                    {
                        result[keyIndex] = columnIndex;
                        found = true;
                        break;
                    }
                }
                if (not found)
                {
                    result[keyIndex] = columns.size();
                }
            }
            return result;
        }
        else
        {
            return std::array<std::size_t, 0>{};
        }
    }
    else
    {
        return makePrimaryKeyIndices<modelPrimaryKeyCount<Model>()>(columns);
    }
}

template <typename Model, typename SchemaType>
inline constexpr auto staticColumns =
    makeColumns<Model, SchemaType>(std::make_index_sequence<reflection::fieldCount<Model>>{});

template <typename Model, typename SchemaType>
inline constexpr auto staticPrimaryKeyIndices = makeModelPrimaryKeyIndices<Model>(staticColumns<Model, SchemaType>);
} // namespace detail
} // namespace orm::model
