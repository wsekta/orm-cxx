module;

#include <array>
#include <concepts>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

export module orm:model;

import orm.reflection;
import :foundation;

// model/Mapping.hpp
namespace orm::model
{
namespace detail
{
template <typename>
inline constexpr bool alwaysFalse = false;

template <typename T>
struct StaticOptionalTraits
{
    inline static constexpr bool isOptional = false;
    using Value = T;
};

template <typename T>
struct StaticOptionalTraits<std::optional<T>>
{
    inline static constexpr bool isOptional = true;
    using Value = T;
};

template <typename T>
using static_optional_value_t = typename StaticOptionalTraits<std::remove_cv_t<T>>::Value;

template <typename T>
struct MemberPointerTraits;

template <typename Value, typename Owner>
struct MemberPointerTraits<Value Owner::*>
{
    using OwnerType = Owner;
    using ValueType = Value;
};

template <auto Member>
using member_owner_t = typename MemberPointerTraits<std::remove_cv_t<decltype(Member)>>::OwnerType;

template <auto Member>
using member_value_t = typename MemberPointerTraits<std::remove_cv_t<decltype(Member)>>::ValueType;

template <auto Member>
[[nodiscard]] consteval auto isPersistentColumnMember() noexcept -> bool
{
    return !orm::is_relation_collection_v<member_value_t<Member>> &&
           !orm::is_optional_relation_collection_v<member_value_t<Member>>;
}

template <auto Member>
inline constexpr auto reflectedMemberNameStorage = reflection::memberName<Member>();

template <auto Member>
consteval decltype(auto) reflectedMemberName()
{
    return (reflectedMemberNameStorage<Member>);
}

template <typename Model, std::size_t Index>
consteval auto reflectedFieldName() -> std::string_view
{
    constexpr auto descriptors = reflection::fields<Model>();
    return descriptors[Index].name;
}

template <typename Model, auto Member, std::size_t... Indices>
consteval auto memberExistsImpl(std::index_sequence<Indices...>) -> bool
{
    constexpr auto memberName = reflectedMemberName<Member>();
    return ((memberName.view().compare(reflectedFieldName<Model, Indices>()) == 0) || ...);
}

template <typename Model, auto Member>
consteval auto memberExists() -> bool
{
    return memberExistsImpl<Model, Member>(std::make_index_sequence<reflection::fieldCount<Model>>{});
}

template <typename Model, auto Member, std::size_t... Indices>
consteval auto memberIndexImpl(std::index_sequence<Indices...>) -> std::size_t
{
    constexpr auto memberName = reflectedMemberName<Member>();
    constexpr std::array<bool, sizeof...(Indices)> matches{
        (memberName.view().compare(reflectedFieldName<Model, Indices>()) == 0)...};
    for (std::size_t index = 0; index < matches.size(); ++index)
    {
        if (matches[index])
        {
            return index;
        }
    }
    return reflection::fieldCount<Model>;
}

template <typename Model, auto Member>
consteval auto memberIndex() -> std::size_t
{
    constexpr auto result = memberIndexImpl<Model, Member>(std::make_index_sequence<reflection::fieldCount<Model>>{});
    static_assert(result < reflection::fieldCount<Model>,
                  "ORM_MODEL_MEMBER: a mapped member is not an aggregate field");
    return result;
}

template <auto... Members>
constexpr auto uniqueMemberNames() -> bool
{
    const auto nameStorage = std::tuple{reflectedMemberName<Members>()...};
    const auto names =
        std::apply([](const auto&... name) { return std::array<std::string_view, sizeof...(Members)>{name.view()...}; },
                   nameStorage);
    for (std::size_t left = 0; left < names.size(); ++left)
    {
        for (std::size_t right = left + 1; right < names.size(); ++right)
        {
            if (names[left].compare(names[right]) == 0)
            {
                return false;
            }
        }
    }
    return true;
}
}
export
{
    // namespace detail

    /**
     * @brief Compile-time override of one aggregate member's SQL column name.
     */
    template <auto Member, reflection::FixedString SqlName>
        requires std::is_member_object_pointer_v<decltype(Member)>
    struct ColumnName
    {
        inline static constexpr auto member = Member;
        inline static constexpr auto sqlName = SqlName;
    };

    /**
     * @brief Creates a typed SQL column-name override.
     */
    template <auto Member, reflection::FixedString SqlName>
        requires std::is_member_object_pointer_v<decltype(Member)>
    consteval auto columnName() -> ColumnName<Member, SqlName>
    {
        static_assert(not SqlName.view().empty(),
                      "ORM_MODEL_COLUMN_MAPPING: a mapped SQL column name must not be empty");
        return {};
    }

    template <typename... Definitions>
    struct ColumnNames
    {
        static_assert((requires {
                          Definitions::member;
                          Definitions::sqlName;
                      } && ...),
                      "ORM_MODEL_COLUMN_MAPPING: columnNames() accepts only columnName<Member, SqlName>() definitions");

        template <typename Model>
        [[nodiscard]] static consteval auto isValidFor() -> bool
        {
            if constexpr (not(std::same_as<detail::member_owner_t<Definitions::member>, Model> && ...))
            {
                return false;
            }
            else if constexpr (not(detail::memberExists<Model, Definitions::member>() && ...))
            {
                return false;
            }
            else if constexpr (not(detail::isPersistentColumnMember<Definitions::member>() && ...))
            {
                return false;
            }
            else if constexpr (not detail::uniqueMemberNames<Definitions::member...>())
            {
                return false;
            }
            else
            {
                return ((not Definitions::sqlName.view().empty()) && ...);
            }
        }

        [[nodiscard]] consteval auto find(std::string_view memberName) const -> std::optional<std::string_view>
        {
            std::string_view result;
            bool found = false;
            (
                [&]
                {
                    if (detail::reflectedMemberName<Definitions::member>() == memberName)
                    {
                        result = Definitions::sqlName.view();
                        found = true;
                    }
                }(),
                ...);
            return found ? std::optional<std::string_view>{result} : std::nullopt;
        }
    };

    /**
     * @brief Groups typed SQL column-name overrides.
     */
    template <typename... Definitions>
    consteval auto columnNames(Definitions...) -> ColumnNames<Definitions...>
    {
        return {};
    }

    template <auto... Members>
        requires(std::is_member_object_pointer_v<decltype(Members)> && ...)
    struct PrimaryKey
    {
        inline static constexpr std::size_t size = sizeof...(Members);

        template <typename Model>
        [[nodiscard]] static consteval auto isValidFor() -> bool
        {
            if constexpr (sizeof...(Members) == 0)
            {
                return true;
            }
            else
            {
                return (std::same_as<detail::member_owner_t<Members>, Model> && ...) &&
                       (detail::memberExists<Model, Members>() && ...) &&
                       (detail::isPersistentColumnMember<Members>() && ...) && detail::uniqueMemberNames<Members...>();
            }
        }

        [[nodiscard]] constexpr auto contains(std::string_view memberName) const -> bool
        {
            if constexpr (sizeof...(Members) == 0)
            {
                return false;
            }
            else
            {
                const auto nameStorage = std::tuple{detail::reflectedMemberName<Members>()...};
                return std::apply([&memberName](const auto&... name)
                                  { return ((name.view().compare(memberName) == 0) || ...); }, nameStorage);
            }
        }

        template <typename Model>
        [[nodiscard]] static consteval auto indices()
        {
            return std::array<std::size_t, sizeof...(Members)>{detail::memberIndex<Model, Members>()...};
        }
    };

    /**
     * @brief Defines an ordered, typed primary key. An empty definition makes the
     * model explicitly keyless.
     */
    template <auto... Members>
        requires(std::is_member_object_pointer_v<decltype(Members)> && ...)
    consteval auto primaryKey() -> PrimaryKey<Members...>
    {
        return {};
    }

    template <auto... Members>
        requires(std::is_member_object_pointer_v<decltype(Members)> && ...)
    struct AutoIncrement
    {
        inline static constexpr std::size_t size = sizeof...(Members);

        template <typename Model>
        [[nodiscard]] static consteval auto isValidFor() -> bool
        {
            if constexpr (sizeof...(Members) == 0)
            {
                return true;
            }
            else
            {
                return (std::same_as<detail::member_owner_t<Members>, Model> && ...) &&
                       (detail::memberExists<Model, Members>() && ...) &&
                       (detail::isPersistentColumnMember<Members>() && ...) && detail::uniqueMemberNames<Members...>();
            }
        }

        [[nodiscard]] constexpr auto contains(std::string_view memberName) const -> bool
        {
            if constexpr (sizeof...(Members) == 0)
            {
                return false;
            }
            else
            {
                const auto nameStorage = std::tuple{detail::reflectedMemberName<Members>()...};
                return std::apply([&memberName](const auto&... name)
                                  { return ((name.view().compare(memberName) == 0) || ...); }, nameStorage);
            }
        }
    };

    /**
     * @brief Defines typed auto-increment members. Static-model validation further
     * restricts this to one non-null int primary-key member.
     */
    template <auto... Members>
        requires(std::is_member_object_pointer_v<decltype(Members)> && ...)
    consteval auto autoIncrement() -> AutoIncrement<Members...>
    {
        return {};
    }

    template <typename T>
    struct LogicalTypeTraits;

    template <>
    struct LogicalTypeTraits<bool>
    {
        inline static constexpr auto value = ColumnType::Bool;
    };

    template <>
    struct LogicalTypeTraits<char>
    {
        inline static constexpr auto value = ColumnType::Char;
    };

    template <>
    struct LogicalTypeTraits<signed char>
    {
        inline static constexpr auto value = ColumnType::Char;
    };

    template <>
    struct LogicalTypeTraits<unsigned char>
    {
        inline static constexpr auto value = ColumnType::UnsignedChar;
    };

    template <>
    struct LogicalTypeTraits<short>
    {
        inline static constexpr auto value = ColumnType::Short;
    };

    template <>
    struct LogicalTypeTraits<unsigned short>
    {
        inline static constexpr auto value = ColumnType::UnsignedShort;
    };

    template <>
    struct LogicalTypeTraits<int>
    {
        inline static constexpr auto value = ColumnType::Int;
    };

    template <>
    struct LogicalTypeTraits<unsigned int>
    {
        inline static constexpr auto value = ColumnType::UnsignedInt;
    };

    template <>
    struct LogicalTypeTraits<long>
    {
        inline static constexpr auto value = sizeof(long) > sizeof(int) ? ColumnType::LongLong : ColumnType::Int;
    };

    template <>
    struct LogicalTypeTraits<unsigned long>
    {
        inline static constexpr auto value =
            sizeof(unsigned long) > sizeof(unsigned int) ? ColumnType::UnsignedLongLong : ColumnType::UnsignedInt;
    };

    template <>
    struct LogicalTypeTraits<long long>
    {
        inline static constexpr auto value = ColumnType::LongLong;
    };

    template <>
    struct LogicalTypeTraits<unsigned long long>
    {
        inline static constexpr auto value = ColumnType::UnsignedLongLong;
    };

    template <>
    struct LogicalTypeTraits<float>
    {
        inline static constexpr auto value = ColumnType::Float;
    };

    template <>
    struct LogicalTypeTraits<double>
    {
        inline static constexpr auto value = ColumnType::Double;
    };

    template <>
    struct LogicalTypeTraits<std::string>
    {
        inline static constexpr auto value = ColumnType::String;
    };

    /**
     * @brief Resolves a C++ field type to its logical database type at compile
     * time. Users may extend the closed set through LogicalTypeTraits<T>.
     */
    template <typename T>
    consteval auto logicalType() -> ColumnType
    {
        using field_t = std::remove_cvref_t<T>;
        using value_t = std::remove_cv_t<detail::static_optional_value_t<field_t>>;

        if constexpr (requires { LogicalTypeTraits<value_t>::value; })
        {
            return LogicalTypeTraits<value_t>::value;
        }
        else
        {
            static_assert(detail::alwaysFalse<value_t>,
                          "ORM_MODEL_LOGICAL_TYPE: unsupported model field type; specialize "
                          "orm::model::LogicalTypeTraits<T>.");
        }
    }

    template <typename T>
    inline constexpr bool isNullable = detail::StaticOptionalTraits<std::remove_cvref_t<T>>::isOptional;
}
} // namespace orm::model

namespace orm
{
export
{

    template <auto Member, reflection::FixedString SqlName>
        requires std::is_member_object_pointer_v<decltype(Member)>
    consteval auto columnName()
    {
        return model::columnName<Member, SqlName>();
    }

    template <typename... Definitions>
    consteval auto columnNames(Definitions... definitions)
    {
        return model::columnNames(definitions...);
    }

    template <auto... Members>
        requires(std::is_member_object_pointer_v<decltype(Members)> && ...)
    consteval auto primaryKey()
    {
        return model::primaryKey<Members...>();
    }

    template <auto... Members>
        requires(std::is_member_object_pointer_v<decltype(Members)> && ...)
    consteval auto autoIncrement()
    {
        return model::autoIncrement<Members...>();
    }

    template <typename T>
    consteval auto logicalType() -> model::ColumnType
    {
        return model::logicalType<T>();
    }
}
} // namespace orm

// model/ModelView.hpp
namespace orm::model
{
export
{

    struct TypeId
    {
        const void* value{};

        constexpr auto operator==(const TypeId&) const noexcept -> bool = default;
    };
}
namespace detail
{
template <typename T>
inline constexpr unsigned char typeToken{};
}
export
{
    // namespace detail

    template <typename T>
    consteval auto typeId() noexcept -> TypeId
    {
        return TypeId{&detail::typeToken<T>};
    }

    enum class FieldKind
    {
        Scalar,
        ToOne,
    };

    inline constexpr auto noTargetModel = std::numeric_limits<std::size_t>::max();

    struct ColumnView
    {
        std::size_t fieldIndex{};
        std::string_view fieldName;
        std::string_view name;
        /**
         * Scalar logical type. To-one fields intentionally have no single type:
         * their physical foreign-key columns inherit the types of the target
         * model's primary-key columns.
         */
        std::optional<ColumnType> type;
        FieldKind kind{FieldKind::Scalar};
        std::size_t targetModelIndex{noTargetModel};
        bool isPrimaryKey{};
        bool isAutoIncrement{};
        bool isNotNull{};

        constexpr auto operator==(const ColumnView&) const noexcept -> bool = default;
    };

    struct JunctionView
    {
        std::string_view tableName;
        std::span<const std::string_view> ownerColumns;
        std::span<const std::string_view> targetColumns;
        bool owningSide{};

        [[nodiscard]] constexpr auto isConfigured() const noexcept -> bool
        {
            return not tableName.empty();
        }
    };

    struct RelationView
    {
        std::size_t fieldIndex{};
        std::string_view fieldName;
        std::string_view columnName;
        RelationKind kind{RelationKind::ToOne};
        std::string_view mappedBy;
        bool nullable{};
        std::size_t targetModelIndex{noTargetModel};
        JunctionView junction{};
    };

    /**
     * @brief Immutable data stored once for a model inside a closed schema.
     */
    struct ModelDataView
    {
        TypeId type;
        std::size_t schemaIndex{};
        std::string_view typeName;
        std::string_view tableName;
        std::span<const ColumnView> columns;
        std::span<const std::size_t> primaryKeyIndices;
        std::span<const RelationView> relations;

        [[nodiscard]] constexpr auto findColumn(std::string_view fieldOrSqlName) const noexcept -> const ColumnView*
        {
            for (std::size_t columnIndex = 0; columnIndex < columns.size(); ++columnIndex)
            {
                const auto& column = columns[columnIndex];
                if (column.fieldName.compare(fieldOrSqlName) == 0 || column.name.compare(fieldOrSqlName) == 0)
                {
                    return &column;
                }
            }
            return nullptr;
        }

        [[nodiscard]] constexpr auto findRelation(std::string_view fieldOrSqlName) const noexcept -> const RelationView*
        {
            for (std::size_t relationIndex = 0; relationIndex < relations.size(); ++relationIndex)
            {
                const auto& relation = relations[relationIndex];
                if (relation.fieldName.compare(fieldOrSqlName) == 0)
                {
                    return &relation;
                }
            }
            for (std::size_t relationIndex = 0; relationIndex < relations.size(); ++relationIndex)
            {
                const auto& relation = relations[relationIndex];
                if (relation.columnName.compare(fieldOrSqlName) == 0)
                {
                    return &relation;
                }
            }
            return nullptr;
        }

        [[nodiscard]] constexpr auto findRelationField(std::string_view fieldName) const noexcept -> const RelationView*
        {
            for (std::size_t relationIndex = 0; relationIndex < relations.size(); ++relationIndex)
            {
                const auto& relation = relations[relationIndex];
                if (relation.fieldName.compare(fieldName) == 0)
                {
                    return &relation;
                }
            }
            return nullptr;
        }
    };

    struct SchemaView;

    /**
     * @brief Trivially-copyable handle to one model in immutable Schema storage.
     */
    struct ModelView
    {
        const SchemaView* schema{};
        std::size_t modelIndex{noTargetModel};

        [[nodiscard]] constexpr auto valid() const noexcept -> bool;
        [[nodiscard]] constexpr explicit operator bool() const noexcept;
        [[nodiscard]] constexpr auto operator==(std::nullptr_t) const noexcept -> bool;
        [[nodiscard]] constexpr auto operator==(const ModelView&) const noexcept -> bool = default;
        [[nodiscard]] constexpr auto operator*() const noexcept -> const ModelView&;
        [[nodiscard]] constexpr auto operator->() const noexcept -> const ModelDataView*;
        [[nodiscard]] constexpr auto data() const noexcept -> const ModelDataView&;

        [[nodiscard]] constexpr auto findColumn(std::string_view fieldOrSqlName) const noexcept -> const ColumnView*;
        [[nodiscard]] constexpr auto
        findRelation(std::string_view fieldOrSqlName) const noexcept -> const RelationView*;
        [[nodiscard]] constexpr auto
        findRelationField(std::string_view fieldName) const noexcept -> const RelationView*;
        [[nodiscard]] constexpr auto resolveTarget(const ColumnView& column) const noexcept -> ModelView;
        [[nodiscard]] constexpr auto resolveTarget(const RelationView& relation) const noexcept -> ModelView;
        [[nodiscard]] constexpr auto resolveJunction(const RelationView& relation) const noexcept -> JunctionView;
        [[nodiscard]] constexpr auto primaryKeySize() const noexcept -> std::size_t;
    };

    /**
     * @brief Immutable view over a statically materialized schema.
     */
    struct SchemaView
    {
        std::span<const ModelDataView* const> models;

        [[nodiscard]] constexpr auto at(std::size_t index) const noexcept -> ModelView
        {
            return ModelView{this, index < models.size() ? index : noTargetModel};
        }

        [[nodiscard]] constexpr auto find(TypeId type) const noexcept -> ModelView
        {
            for (std::size_t index = 0; index < models.size(); ++index)
            {
                if (models[index]->type == type)
                {
                    return at(index);
                }
            }
            return ModelView{this, noTargetModel};
        }

        [[nodiscard]] constexpr auto findTable(std::string_view tableName) const noexcept -> ModelView
        {
            for (std::size_t index = 0; index < models.size(); ++index)
            {
                if (models[index]->tableName.compare(tableName) == 0)
                {
                    return at(index);
                }
            }
            return ModelView{this, noTargetModel};
        }
    };

} // exported model view types

constexpr auto ModelView::valid() const noexcept -> bool
{
    return schema != nullptr && modelIndex < schema->models.size() && schema->models[modelIndex] != nullptr;
}

constexpr ModelView::operator bool() const noexcept
{
    return valid();
}

constexpr auto ModelView::operator==(std::nullptr_t) const noexcept -> bool
{
    return not valid();
}

constexpr auto ModelView::operator*() const noexcept -> const ModelView&
{
    return *this;
}

constexpr auto ModelView::operator->() const noexcept -> const ModelDataView*
{
    return valid() ? schema->models[modelIndex] : nullptr;
}

constexpr auto ModelView::data() const noexcept -> const ModelDataView&
{
    return *schema->models[modelIndex];
}

constexpr auto ModelView::findColumn(std::string_view fieldOrSqlName) const noexcept -> const ColumnView*
{
    return valid() ? data().findColumn(fieldOrSqlName) : nullptr;
}

constexpr auto ModelView::findRelation(std::string_view fieldOrSqlName) const noexcept -> const RelationView*
{
    return valid() ? data().findRelation(fieldOrSqlName) : nullptr;
}

constexpr auto ModelView::findRelationField(std::string_view fieldName) const noexcept -> const RelationView*
{
    return valid() ? data().findRelationField(fieldName) : nullptr;
}

constexpr auto ModelView::resolveTarget(const ColumnView& column) const noexcept -> ModelView
{
    if (not valid() || column.kind != FieldKind::ToOne || column.targetModelIndex == noTargetModel)
    {
        return {};
    }
    return schema->at(column.targetModelIndex);
}

constexpr auto ModelView::resolveTarget(const RelationView& relation) const noexcept -> ModelView
{
    if (not valid() || relation.targetModelIndex == noTargetModel)
    {
        return {};
    }
    return schema->at(relation.targetModelIndex);
}

constexpr auto ModelView::resolveJunction(const RelationView& relation) const noexcept -> JunctionView
{
    if (relation.junction.isConfigured())
    {
        return relation.junction;
    }
    if (relation.kind != RelationKind::ManyToMany || relation.mappedBy.empty())
    {
        return {};
    }

    const auto target = resolveTarget(relation);
    const auto* owningRelation = target ? target.findRelationField(relation.mappedBy) : nullptr;
    if (owningRelation == nullptr || owningRelation->kind != RelationKind::ManyToMany ||
        not owningRelation->junction.isConfigured())
    {
        return {};
    }

    return JunctionView{
        .tableName = owningRelation->junction.tableName,
        .ownerColumns = owningRelation->junction.targetColumns,
        .targetColumns = owningRelation->junction.ownerColumns,
        .owningSide = false,
    };
}

constexpr auto ModelView::primaryKeySize() const noexcept -> std::size_t
{
    return valid() ? data().primaryKeyIndices.size() : 0U;
}

static_assert(std::is_trivially_copyable_v<ModelView>);
static_assert(sizeof(ModelView) == sizeof(const SchemaView*) + sizeof(std::size_t));
static_assert(std::is_trivially_copyable_v<SchemaView>);
} // namespace orm::model

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

template <typename Model>
consteval auto relationDefinitions()
{
    if constexpr (requires { Model::relations; })
    {
        using definition_t = std::remove_cvref_t<decltype(Model::relations)>;
        if constexpr (IsRelations<definition_t>::value)
        {
            return Model::relations;
        }
        else
        {
            return std::tuple{};
        }
    }
    else
    {
        return std::tuple{};
    }
}

template <typename Model>
using relation_definitions_t = std::remove_cvref_t<decltype(relationDefinitions<Model>())>;

template <typename Model>
consteval auto hasValidRelationsDefinition() -> bool
{
    if constexpr (requires { Model::relations; })
    {
        using definition_t = std::remove_cvref_t<decltype(Model::relations)>;
        return IsRelations<definition_t>::value;
    }
    return true;
}

template <typename Descriptor>
consteval auto relationFieldName() -> std::string_view
{
    return reflectedMemberNameStorage<Descriptor::member>.view();
}

template <typename Descriptor>
consteval auto relationMappedByName() -> std::string_view
{
    if constexpr (std::is_same_v<std::remove_cv_t<decltype(Descriptor::mappedByMember)>, std::nullptr_t>)
    {
        return Descriptor::mappedByName.view();
    }
    else
    {
        return reflectedMemberNameStorage<Descriptor::mappedByMember>.view();
    }
}

template <typename Descriptor>
using relation_field_t = std::remove_cvref_t<orm::detail::relation_member_value_t<Descriptor::member>>;

template <typename Descriptor>
using relation_target_t = orm::relation_target_t<relation_field_t<Descriptor>>;

template <typename Model, typename Descriptor>
consteval auto isValidRelationDescriptor() -> bool
{
    if constexpr (not orm::detail::IsRelationDescriptor<Descriptor>::value)
    {
        return false;
    }
    else if constexpr (not std::same_as<orm::detail::relation_member_owner_t<Descriptor::member>, Model>)
    {
        return false;
    }
    else if constexpr (not memberExists<Model, Descriptor::member>())
    {
        return false;
    }
    else if constexpr (orm::detail::IsOneToManyDescriptor<Descriptor>::value)
    {
        return is_one_to_many_v<relation_field_t<Descriptor>>;
    }
    else
    {
        return is_relation_collection_v<relation_field_t<Descriptor>> &&
               not is_one_to_many_v<relation_field_t<Descriptor>>;
    }
}

template <typename Model, typename... Descriptors>
consteval auto hasUniqueValidRelationDescriptors(std::tuple<Descriptors...>) -> bool
{
    if constexpr (not(isValidRelationDescriptor<Model, Descriptors>() && ...))
    {
        return false;
    }
    else
    {
        constexpr std::array<std::string_view, sizeof...(Descriptors)> names{relationFieldName<Descriptors>()...};
        bool unique = true;
        for (std::size_t left = 0; left < names.size() && unique; ++left)
        {
            for (std::size_t right = left + 1; right < names.size() && unique; ++right)
            {
                if (names[left].compare(names[right]) == 0)
                {
                    unique = false;
                }
            }
        }
        return unique;
    }
}

template <typename Model, std::size_t... Indices>
consteval auto collectionFieldCount(std::index_sequence<Indices...>) -> std::size_t
{
    return ((is_relation_collection_v<reflection::field_type_t<Model, Indices>> ? 1U : 0U) + ... + 0U);
}

template <typename Model>
consteval auto hasCompleteRelationDefinitions() -> bool
{
    constexpr auto definitions = relationDefinitions<Model>();
    return hasValidRelationsDefinition<Model>() && hasUniqueValidRelationDescriptors<Model>(definitions) &&
           std::tuple_size_v<relation_definitions_t<Model>> ==
               collectionFieldCount<Model>(std::make_index_sequence<reflection::fieldCount<Model>>{});
}

template <typename Model>
consteval auto validatedRelationDefinitions()
{
    if constexpr (hasCompleteRelationDefinitions<Model>())
    {
        return relationDefinitions<Model>();
    }
    else
    {
        return std::tuple{};
    }
}

template <typename Model, typename SchemaType>
inline constexpr auto staticColumns =
    makeColumns<Model, SchemaType>(std::make_index_sequence<reflection::fieldCount<Model>>{});

template <typename Model, typename SchemaType>
inline constexpr auto staticPrimaryKeyIndices = makeModelPrimaryKeyIndices<Model>(staticColumns<Model, SchemaType>);

template <std::size_t Capacity>
struct RelationNameBuffer
{
    std::array<char, Capacity> data{};
    std::size_t size{};

    [[nodiscard]] constexpr auto view() const noexcept -> std::string_view
    {
        return {data.data(), size};
    }
};

template <typename Model, typename SchemaType>
struct DefaultJunctionColumnStorage
{
    inline static constexpr auto& columns = staticColumns<Model, SchemaType>;
    inline static constexpr auto& primaryKeyIndices = staticPrimaryKeyIndices<Model, SchemaType>;
    inline static constexpr auto capacity = []
    {
        std::size_t result = mappedTableName<Model>().size() + 2;
        for (const auto columnIndex : primaryKeyIndices)
        {
            result = result < mappedTableName<Model>().size() + 1 + columns[columnIndex].name.size() + 1 ?
                         mappedTableName<Model>().size() + 1 + columns[columnIndex].name.size() + 1 :
                         result;
        }
        return result;
    }();

    inline static constexpr auto buffers = []
    {
        std::array<RelationNameBuffer<capacity>, primaryKeyIndices.size()> result{};
        for (std::size_t keyIndex = 0; keyIndex < primaryKeyIndices.size(); ++keyIndex)
        {
            auto& output = result[keyIndex];
            const auto table = mappedTableName<Model>();
            const auto column = columns[primaryKeyIndices[keyIndex]].name;
            for (const auto character : table)
            {
                output.data[output.size++] = character;
            }
            output.data[output.size++] = '_';
            for (const auto character : column)
            {
                output.data[output.size++] = character;
            }
        }
        return result;
    }();

    inline static constexpr auto names = []
    {
        std::array<std::string_view, primaryKeyIndices.size()> result{};
        for (std::size_t index = 0; index < result.size(); ++index)
        {
            result[index] = buffers[index].view();
        }
        return result;
    }();
};

template <typename Descriptor, typename Owner, typename Target, typename SchemaType>
struct JunctionStorage
{
    inline static constexpr auto& defaultOwner = DefaultJunctionColumnStorage<Owner, SchemaType>::names;
    inline static constexpr auto& defaultTarget = DefaultJunctionColumnStorage<Target, SchemaType>::names;
    inline static constexpr auto& owner = []() -> const auto&
    {
        if constexpr (Descriptor::OwnerColumns::size == 0)
        {
            return defaultOwner;
        }
        else
        {
            return Descriptor::OwnerColumns::values;
        }
    }();
    inline static constexpr auto& target = []() -> const auto&
    {
        if constexpr (Descriptor::TargetColumns::size == 0)
        {
            return defaultTarget;
        }
        else
        {
            return Descriptor::TargetColumns::values;
        }
    }();
};

template <typename Model, typename SchemaType, std::size_t Index>
consteval auto isToOneField() -> bool
{
    using field_t = reflection::field_type_t<Model, Index>;
    using value_t = std::remove_cv_t<static_optional_value_t<std::remove_cvref_t<field_t>>>;
    return not is_relation_collection_v<field_t> && StaticSchemaTraits<SchemaType>::template contains<value_t>;
}

template <typename Model, typename SchemaType, std::size_t... Indices>
consteval auto makeToOneRelations(std::index_sequence<Indices...>)
{
    constexpr auto count = ((isToOneField<Model, SchemaType, Indices>() ? 1U : 0U) + ... + 0U);
    std::array<RelationView, count> result{};
    std::size_t output{};
    (
        [&]
        {
            if constexpr (isToOneField<Model, SchemaType, Indices>())
            {
                using field_t = reflection::field_type_t<Model, Indices>;
                using target_t = std::remove_cv_t<static_optional_value_t<std::remove_cvref_t<field_t>>>;
                constexpr auto fieldName = reflectedFieldName<Model, Indices>();
                result[output++] = RelationView{
                    .fieldIndex = Indices,
                    .fieldName = fieldName,
                    .columnName = mappedColumnName<Model>(fieldName),
                    .kind = RelationKind::ToOne,
                    .mappedBy = {},
                    .nullable = isNullable<field_t>,
                    .targetModelIndex = StaticSchemaTraits<SchemaType>::template indexOf<target_t>(),
                    .junction = {},
                };
            }
        }(),
        ...);
    return result;
}

template <typename Model, typename SchemaType, typename Descriptor>
consteval auto makeCollectionRelation() -> RelationView
{
    using target_t = relation_target_t<Descriptor>;
    if constexpr (not StaticSchemaTraits<SchemaType>::template contains<target_t>)
    {
        static_assert(StaticSchemaTraits<SchemaType>::template contains<target_t>,
                      "ORM_RELATION_TARGET_MISSING: collection target must belong to Schema");
        return {};
    }
    else
    {
        static_assert(hasPrimaryKey<Model>() && hasPrimaryKey<target_t>(),
                      "ORM_RELATION_KEYLESS: collection endpoints must define primary keys");

        constexpr auto fieldName = relationFieldName<Descriptor>();
        constexpr auto mappedBy = relationMappedByName<Descriptor>();
        RelationView result{
            .fieldIndex = memberIndex<Model, Descriptor::member>(),
            .fieldName = fieldName,
            .columnName = fieldName,
            .kind = orm::detail::IsOneToManyDescriptor<Descriptor>::value ? RelationKind::OneToMany :
                                                                            RelationKind::ManyToMany,
            .mappedBy = mappedBy,
            .nullable = false,
            .targetModelIndex = StaticSchemaTraits<SchemaType>::template indexOf<target_t>(),
            .junction = {},
        };

        if constexpr (orm::detail::IsOneToManyDescriptor<Descriptor>::value)
        {
            static_assert(not mappedBy.empty(), "ORM_RELATION_ONE_TO_MANY_MAPPED_BY: one-to-many requires mappedBy");
        }
        else
        {
            constexpr auto isInverse = not mappedBy.empty();
            if constexpr (isInverse)
            {
                static_assert(Descriptor::throughTable.view().empty() && Descriptor::OwnerColumns::size == 0 &&
                                  Descriptor::TargetColumns::size == 0,
                              "ORM_RELATION_INVERSE_CONFIG: inverse many-to-many may only use mappedBy");
            }
            else
            {
                static_assert(not Descriptor::throughTable.view().empty(),
                              "ORM_RELATION_JUNCTION_MISSING: owning many-to-many requires through");
                using storage_t = JunctionStorage<Descriptor, Model, target_t, SchemaType>;
                static_assert(storage_t::owner.size() == staticPrimaryKeyIndices<Model, SchemaType>.size(),
                              "ORM_RELATION_OWNER_COLUMN_COUNT: owner junction columns must match owner PK");
                static_assert(storage_t::target.size() == staticPrimaryKeyIndices<target_t, SchemaType>.size(),
                              "ORM_RELATION_TARGET_COLUMN_COUNT: target junction columns must match target PK");
                result.junction = JunctionView{
                    .tableName = Descriptor::throughTable.view(),
                    .ownerColumns = storage_t::owner,
                    .targetColumns = storage_t::target,
                    .owningSide = true,
                };
            }
        }
        return result;
    }
}

template <typename Model, typename SchemaType, typename... Descriptors>
consteval auto makeCollectionRelations(std::tuple<Descriptors...>)
{
    return std::array<RelationView, sizeof...(Descriptors)>{
        makeCollectionRelation<Model, SchemaType, Descriptors>()...};
}

template <typename Model, typename SchemaType>
consteval auto makeRelations()
{
    constexpr auto toOne =
        makeToOneRelations<Model, SchemaType>(std::make_index_sequence<reflection::fieldCount<Model>>{});
    constexpr auto collections = makeCollectionRelations<Model, SchemaType>(validatedRelationDefinitions<Model>());
    std::array<RelationView, toOne.size() + collections.size()> result{};
    std::size_t output{};
    for (const auto& relation : toOne)
    {
        result[output++] = relation;
    }
    for (const auto& relation : collections)
    {
        result[output++] = relation;
    }
    return result;
}

template <typename... Models>
consteval auto hasUniqueTypes() -> bool
{
    constexpr std::array<TypeId, sizeof...(Models)> ids{typeId<Models>()...};
    bool unique = true;
    for (std::size_t left = 0; left < ids.size() && unique; ++left)
    {
        for (std::size_t right = left + 1; right < ids.size() && unique; ++right)
        {
            if (ids[left] == ids[right])
            {
                unique = false;
            }
        }
    }
    return unique;
}
}
export
{
    // namespace detail

    /**
     * @brief Materializes one aggregate model's complete scalar metadata in static
     * storage.
     */
    template <typename T, typename SchemaType>
    struct StaticModel
    {
        static_assert(detail::StaticSchemaTraits<SchemaType>::template contains<T>,
                      "ORM_SCHEMA_MODEL_MISSING: a static descriptor requires its model to belong to Schema");
        static_assert(std::is_aggregate_v<T>, "ORM_MODEL_AGGREGATE: an ORM model must be an aggregate");
        static_assert(reflection::fieldCount<T> <= 128,
                      "ORM_MODEL_FIELD_LIMIT: automatic model reflection supports at most 128 fields");
        static_assert(detail::hasValidTableNameDefinition<T>(),
                      "ORM_MODEL_TABLE_NAME: table_name must be a non-empty orm::reflection::FixedString");
        static_assert(detail::hasValidColumnNamesDefinition<T>(),
                      "ORM_MODEL_COLUMN_MAPPING: columns_names must be a valid typed columnNames(...) definition");
        static_assert(
            detail::hasValidPrimaryKeyDefinition<T>(),
            "ORM_MODEL_PRIMARY_KEY_DEFINITION: id_columns must be a valid typed primaryKey<Members...>() definition");
        static_assert(detail::hasValidAutoIncrementDefinition<T>(),
                      "ORM_MODEL_AUTO_INCREMENT_DEFINITION: auto_increment_columns must be a valid typed "
                      "autoIncrement<Members...>() definition");
        static_assert(detail::hasValidRelationsDefinition<T>(),
                      "ORM_MODEL_RELATIONS_DEFINITION: relations must be a typed orm::relations(...) definition");
        static_assert(detail::hasCompleteRelationDefinitions<T>(),
                      "ORM_RELATION_DESCRIPTOR_COUNT: every collection requires exactly one matching descriptor");

        inline static constexpr auto columns = detail::staticColumns<T, SchemaType>;

        static_assert(detail::hasValidColumnNames(columns),
                      "ORM_MODEL_COLUMN_NAME: model field and SQL column names must be non-empty and unique");
        static_assert(detail::hasValidPrimaryKey(columns),
                      "ORM_MODEL_PRIMARY_KEY: primary-key members must be non-null scalar fields");
        static_assert(detail::hasValidAutoIncrement(columns),
                      "ORM_MODEL_AUTO_INCREMENT: auto increment requires one "
                      "non-null int member which is the model's only primary key");

        inline static constexpr auto primaryKeyIndices = detail::staticPrimaryKeyIndices<T, SchemaType>;
        static_assert(
            []
            {
                for (const auto index : primaryKeyIndices)
                {
                    if (index >= columns.size())
                    {
                        return false;
                    }
                }
                return true;
            }(),
            "ORM_MODEL_PRIMARY_KEY_RELATION: primary keys must refer to scalar columns");

        inline static constexpr auto relations = detail::makeRelations<T, SchemaType>();

        inline static constexpr ModelDataView data{
            .type = typeId<T>(),
            .schemaIndex = detail::StaticSchemaTraits<SchemaType>::template indexOf<T>(),
            .typeName = detail::modelTypeName<T>(),
            .tableName = detail::mappedTableName<T>(),
            .columns = columns,
            .primaryKeyIndices = primaryKeyIndices,
            .relations = relations,
        };
    };

    template <typename SchemaType, typename T>
    [[nodiscard]] consteval auto modelDescriptor() noexcept -> StaticModel<T, SchemaType>
    {
        return {};
    }

    template <typename SchemaType, typename T>
    [[nodiscard]] constexpr auto modelView() noexcept -> ModelView
    {
        return ModelView{&SchemaType::view, detail::StaticSchemaTraits<SchemaType>::template indexOf<T>()};
    }

    template <typename T>
    [[nodiscard]] consteval auto tableName() -> std::string_view
    {
        return detail::mappedTableName<T>();
    }
}
} // namespace orm::model

// model/Schema.hpp
namespace orm::model
{
namespace detail
{
template <typename... Models>
consteval auto hasUniqueTableNames() -> bool
{
    constexpr std::array<std::string_view, sizeof...(Models)> names{tableName<Models>()...};
    for (std::size_t left = 0; left < names.size(); ++left)
    {
        for (std::size_t right = left + 1; right < names.size(); ++right)
        {
            if (names[left].compare(names[right]) == 0)
            {
                return false;
            }
        }
    }
    return true;
}

template <std::size_t Size>
consteval auto hasValidRelationTargets(const std::array<const ModelDataView*, Size>& models) -> bool
{
    for (const auto* owner : models)
    {
        for (std::size_t relationIndex = 0; relationIndex < owner->relations.size(); ++relationIndex)
        {
            const auto& relation = owner->relations[relationIndex];
            if (relation.targetModelIndex >= models.size())
            {
                return false;
            }
            const auto* target = models[relation.targetModelIndex];
            if (target == nullptr || target->primaryKeyIndices.empty())
            {
                return false;
            }

            if (relation.kind == RelationKind::OneToMany)
            {
                if (relation.mappedBy.empty())
                {
                    return false;
                }
                const auto* inverse = target->findRelationField(relation.mappedBy);
                if (inverse == nullptr || inverse->kind != RelationKind::ToOne ||
                    inverse->targetModelIndex != owner->schemaIndex)
                {
                    return false;
                }
            }
            else if (relation.kind == RelationKind::ManyToMany && not relation.junction.isConfigured())
            {
                if (relation.mappedBy.empty())
                {
                    return false;
                }
                const auto* owning = target->findRelationField(relation.mappedBy);
                if (owning == nullptr || owning->kind != RelationKind::ManyToMany ||
                    owning->targetModelIndex != owner->schemaIndex || not owning->junction.isConfigured() ||
                    not owning->junction.owningSide)
                {
                    return false;
                }
            }
        }
    }
    return true;
}

[[nodiscard]] consteval auto foreignKeyColumnNameSize(std::string_view relationName,
                                                      std::string_view targetColumnName) -> std::size_t
{
    return relationName.size() + 1U + targetColumnName.size();
}

[[nodiscard]] consteval auto foreignKeyColumnNameCharacter(std::string_view relationName,
                                                           std::string_view targetColumnName, std::size_t index) -> char
{
    if (index < relationName.size())
    {
        return relationName[index];
    }
    if (index == relationName.size())
    {
        return '_';
    }
    return targetColumnName[index - relationName.size() - 1U];
}

[[nodiscard]] consteval auto equalsForeignKeyColumnName(std::string_view candidate, std::string_view relationName,
                                                        std::string_view targetColumnName) -> bool
{
    if (candidate.size() != foreignKeyColumnNameSize(relationName, targetColumnName))
    {
        return false;
    }
    for (std::size_t index = 0; index < candidate.size(); ++index)
    {
        if (candidate[index] != foreignKeyColumnNameCharacter(relationName, targetColumnName, index))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] consteval auto equalForeignKeyColumnNames(std::string_view leftRelation,
                                                        std::string_view leftTargetColumn,
                                                        std::string_view rightRelation,
                                                        std::string_view rightTargetColumn) -> bool
{
    const auto size = foreignKeyColumnNameSize(leftRelation, leftTargetColumn);
    if (size != foreignKeyColumnNameSize(rightRelation, rightTargetColumn))
    {
        return false;
    }
    for (std::size_t index = 0; index < size; ++index)
    {
        if (foreignKeyColumnNameCharacter(leftRelation, leftTargetColumn, index) !=
            foreignKeyColumnNameCharacter(rightRelation, rightTargetColumn, index))
        {
            return false;
        }
    }
    return true;
}

template <std::size_t Size>
consteval auto hasUniquePhysicalColumnNames(const std::array<const ModelDataView*, Size>& models) -> bool
{
    for (const auto* owner : models)
    {
        for (std::size_t scalarIndex = 0; scalarIndex < owner->columns.size(); ++scalarIndex)
        {
            const auto& scalar = owner->columns[scalarIndex];
            if (scalar.kind != FieldKind::Scalar)
            {
                continue;
            }
            for (std::size_t relationColumnIndex = 0; relationColumnIndex < owner->columns.size();
                 ++relationColumnIndex)
            {
                const auto& relation = owner->columns[relationColumnIndex];
                if (relation.kind != FieldKind::ToOne || relation.targetModelIndex >= models.size())
                {
                    continue;
                }
                const auto* target = models[relation.targetModelIndex];
                for (std::size_t keyIndex = 0; keyIndex < target->primaryKeyIndices.size(); ++keyIndex)
                {
                    const auto primaryKeyIndex = target->primaryKeyIndices[keyIndex];
                    if (primaryKeyIndex >= target->columns.size() ||
                        equalsForeignKeyColumnName(scalar.name, relation.name, target->columns[primaryKeyIndex].name))
                    {
                        return false;
                    }
                }
            }
        }

        for (std::size_t leftRelationIndex = 0; leftRelationIndex < owner->columns.size(); ++leftRelationIndex)
        {
            const auto& leftRelation = owner->columns[leftRelationIndex];
            if (leftRelation.kind != FieldKind::ToOne || leftRelation.targetModelIndex >= models.size())
            {
                continue;
            }
            const auto* leftTarget = models[leftRelation.targetModelIndex];
            for (std::size_t leftKeyIndex = 0; leftKeyIndex < leftTarget->primaryKeyIndices.size(); ++leftKeyIndex)
            {
                const auto leftPrimaryKeyIndex = leftTarget->primaryKeyIndices[leftKeyIndex];
                if (leftPrimaryKeyIndex >= leftTarget->columns.size())
                {
                    return false;
                }
                for (std::size_t rightRelationIndex = leftRelationIndex + 1; rightRelationIndex < owner->columns.size();
                     ++rightRelationIndex)
                {
                    const auto& rightRelation = owner->columns[rightRelationIndex];
                    if (rightRelation.kind != FieldKind::ToOne || rightRelation.targetModelIndex >= models.size())
                    {
                        continue;
                    }
                    const auto* rightTarget = models[rightRelation.targetModelIndex];
                    for (std::size_t rightKeyIndex = 0; rightKeyIndex < rightTarget->primaryKeyIndices.size();
                         ++rightKeyIndex)
                    {
                        const auto rightPrimaryKeyIndex = rightTarget->primaryKeyIndices[rightKeyIndex];
                        if (rightPrimaryKeyIndex >= rightTarget->columns.size() ||
                            equalForeignKeyColumnNames(leftRelation.name, leftTarget->columns[leftPrimaryKeyIndex].name,
                                                       rightRelation.name,
                                                       rightTarget->columns[rightPrimaryKeyIndex].name))
                        {
                            return false;
                        }
                    }
                }
            }
        }
    }
    return true;
}

[[nodiscard]] consteval auto hasUniqueNames(std::span<const std::string_view> left,
                                            std::span<const std::string_view> right = {}) -> bool
{
    for (std::size_t leftIndex = 0; leftIndex < left.size(); ++leftIndex)
    {
        if (left[leftIndex].empty())
        {
            return false;
        }
        for (std::size_t other = leftIndex + 1; other < left.size(); ++other)
        {
            if (left[leftIndex].compare(left[other]) == 0)
            {
                return false;
            }
        }
        for (std::size_t rightIndex = 0; rightIndex < right.size(); ++rightIndex)
        {
            const auto rightName = right[rightIndex];
            if (rightName.empty() || left[leftIndex].compare(rightName) == 0)
            {
                return false;
            }
        }
    }
    for (std::size_t rightIndex = 0; rightIndex < right.size(); ++rightIndex)
    {
        if (right[rightIndex].empty())
        {
            return false;
        }
        for (std::size_t other = rightIndex + 1; other < right.size(); ++other)
        {
            if (right[rightIndex].compare(right[other]) == 0)
            {
                return false;
            }
        }
    }
    return true;
}

template <std::size_t Size>
consteval auto hasValidJunctions(const std::array<const ModelDataView*, Size>& models) -> bool
{
    for (std::size_t ownerIndex = 0; ownerIndex < models.size(); ++ownerIndex)
    {
        const auto* owner = models[ownerIndex];
        for (std::size_t relationIndex = 0; relationIndex < owner->relations.size(); ++relationIndex)
        {
            const auto& relation = owner->relations[relationIndex];
            if (relation.kind != RelationKind::ManyToMany || not relation.junction.isConfigured())
            {
                continue;
            }
            const auto* target = models[relation.targetModelIndex];
            if (not relation.mappedBy.empty() || not relation.junction.owningSide ||
                relation.junction.ownerColumns.size() != owner->primaryKeyIndices.size() ||
                relation.junction.targetColumns.size() != target->primaryKeyIndices.size() ||
                not hasUniqueNames(relation.junction.ownerColumns, relation.junction.targetColumns))
            {
                return false;
            }
            for (const auto* model : models)
            {
                if (model->tableName.compare(relation.junction.tableName) == 0)
                {
                    return false;
                }
            }
            for (std::size_t otherOwnerIndex = ownerIndex; otherOwnerIndex < models.size(); ++otherOwnerIndex)
            {
                const auto* otherOwner = models[otherOwnerIndex];
                const auto firstRelation = otherOwnerIndex == ownerIndex ? relationIndex + 1 : std::size_t{};
                for (std::size_t otherRelationIndex = firstRelation; otherRelationIndex < otherOwner->relations.size();
                     ++otherRelationIndex)
                {
                    const auto& other = otherOwner->relations[otherRelationIndex];
                    if (other.kind == RelationKind::ManyToMany && other.junction.isConfigured() &&
                        other.junction.tableName.compare(relation.junction.tableName) == 0)
                    {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}
}
export
{
    // namespace detail

    /**
     * @brief A closed compile-time set of ORM models.
     */
    template <typename... Models>
    struct Schema
    {
        static_assert(detail::hasUniqueTypes<Models...>(),
                      "ORM_SCHEMA_DUPLICATE_MODEL: a schema may list a model type only once");
        static_assert(detail::hasUniqueTableNames<Models...>(),
                      "ORM_SCHEMA_DUPLICATE_TABLE: every model in a schema must use a unique table name");

        template <typename Model>
        inline static constexpr bool contains = (std::same_as<Model, Models> || ...);

        template <typename Model>
        [[nodiscard]] static consteval auto indexOf() -> std::size_t
        {
            static_assert(contains<Model>,
                          "ORM_SCHEMA_MODEL_MISSING: requested model type does not belong to this schema");
            constexpr std::array<bool, sizeof...(Models)> matches{std::same_as<Model, Models>...};
            for (std::size_t index = 0; index < matches.size(); ++index)
            {
                if (matches[index])
                {
                    return index;
                }
            }
            return 0;
        }

        [[nodiscard]] static constexpr auto modelAt(std::size_t index) noexcept -> ModelView
        {
            return view.at(index);
        }

        inline static constexpr std::array<const ModelDataView*, sizeof...(Models)> modelStorage{
            &StaticModel<Models, Schema<Models...>>::data...};

        static_assert(detail::hasUniquePhysicalColumnNames(modelStorage),
                      "ORM_SCHEMA_COLUMN_COLLISION: scalar and generated foreign-key column names must be unique");

        static_assert(detail::hasValidRelationTargets(modelStorage),
                      "ORM_SCHEMA_RELATION_GRAPH: mappedBy must resolve to a compatible relation in the closed schema");
        static_assert(
            detail::hasValidJunctions(modelStorage),
            "ORM_SCHEMA_JUNCTION: junction tables and columns must be unique and match endpoint primary keys");

        inline static constexpr SchemaView view{.models = modelStorage};
    };

    template <typename SchemaType>
    [[nodiscard]] constexpr auto schemaView() noexcept -> const SchemaView&
    {
        return SchemaType::view;
    }

    template <typename SchemaType, typename T>
    consteval auto requireSchemaModel() -> void
    {
        static_assert(SchemaType::template contains<std::remove_cv_t<T>>,
                      "ORM_SCHEMA_MODEL_MISSING: requested model type does not belong to the closed schema");
    }
}
} // namespace orm::model

namespace orm
{
export
{

    template <typename... Models>
    using Schema = model::Schema<Models...>;

    template <typename SchemaType, typename T>
    [[nodiscard]] consteval auto modelDescriptor() noexcept -> model::StaticModel<T, SchemaType>
    {
        return model::modelDescriptor<SchemaType, T>();
    }

    template <typename SchemaType, typename T>
    [[nodiscard]] constexpr auto modelView() noexcept -> model::ModelView
    {
        return model::modelView<SchemaType, T>();
    }
}
} // namespace orm
