module;

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

export module orm:database_result_storage;

import orm.reflection;
import :foundation;
import :model;
import :expressions;
import :sql;
import :value_codec;
import :database_numeric;

namespace orm::db::binding
{
struct ResultColumn
{
    std::string alias;
    model::ColumnType logicalType;
    std::optional<model::ColumnType> requestedType = std::nullopt;
};

inline auto isEncodedResultType(model::ColumnType type) noexcept -> bool
{
    using enum model::ColumnType;
    return type == Date || type == TimeOfDay || type == LocalDateTime || type == UtcTimestamp || type == Uuid;
}

inline auto hasEncodedResult(const std::vector<ResultColumn>& columns) -> bool
{
    return std::ranges::any_of(columns,
                               [](const auto& column)
                               {
                                   return isEncodedResultType(column.logicalType) ||
                                          (column.requestedType && isEncodedResultType(*column.requestedType));
                               });
}

// Keep this ordering identical to emitSqlFullFields, including the flattened
// to-one aliases used by joined and non-joined hydration.
inline auto modelResultColumns(model::ModelView model, bool joined) -> std::vector<ResultColumn>
{
    std::vector<ResultColumn> result;
    for (const auto& column : model->columns)
    {
        if (column.kind == model::FieldKind::Scalar)
        {
            result.push_back({std::string{model->tableName} + "_" + std::string{column.name}, column.type.value()});
        }
        else
        {
            const auto target = model.resolveTarget(column);
            if (!target)
                throw ConversionError{"A related result model is not available in its schema"};
            for (const auto& child : target->columns)
            {
                if (joined || child.isPrimaryKey)
                {
                    const auto prefix = joined ? std::string{column.name} :
                                                 std::string{model->tableName} + "_" + std::string{column.name};
                    result.push_back({prefix + "_" + std::string{child.name}, child.type.value()});
                }
            }
        }
    }
    return result;
}

inline auto resultSource(const query::detail::Column& column) -> db::detail::SqlSource
{
    return db::detail::parseSqlSource(column.getPath());
}

inline auto resultSource(const query::detail::AggregateExpression& expression) -> db::detail::SqlSource
{
    auto source = expression.column ? resultSource(*expression.column) : db::detail::SqlSource{};
    source.isAggregate = true;
    source.function = expression.function;
    return source;
}

inline auto resultSourceType(const db::detail::SqlSource& source, model::ModelView model) -> model::ColumnType
{
    if (source.isAggregate)
    {
        using enum query::detail::AggregateFunction;
        if (source.function == Count || source.function == CountAll)
            return model::ColumnType::LongLong;
        // The existing emitter transports SUM and AVG through an explicit text
        // result to preserve decimal conversion checks.
        if (source.function == Sum || source.function == Avg)
            return model::ColumnType::String;
    }
    return db::detail::resolveSqlColumn(source, model).logicalType;
}

template <typename Result, typename Projections>
auto projectionResultColumns(model::ModelView model, const Projections& projections) -> std::vector<ResultColumn>
{
    std::vector<ResultColumn> result;
    result.reserve(projections.size());
    for (const auto& projection : projections)
    {
        if constexpr (requires { projection.alias; })
            result.push_back({std::string{projection.alias}, resultSourceType(projection.source, model)});
        else
        {
            const auto source = std::visit([](const auto& value) { return resultSource(value); }, projection.source);
            result.push_back({projection.resultField, resultSourceType(source, model)});
        }
        [&]<std::size_t... Indices>(std::index_sequence<Indices...>)
        {
            constexpr auto fields = reflection::fields<Result>();
            auto matchField = [&]<std::size_t Index>()
            {
                using field_t = optional_value_t<reflection::field_type_t<Result, Index>>;
                if constexpr (requires { model::LogicalTypeTraits<field_t>::value; })
                    if (result.back().alias == fields[Index].name)
                        result.back().requestedType = model::LogicalTypeTraits<field_t>::value;
            };
            (matchField.template operator()<Indices>(), ...);
        }(std::make_index_sequence<reflection::fieldCount<Result>>{});
    }
    return result;
}

/**
 * Owns stable per-column buffers for explicit SOCI exchanges. New scalar types
 * always use into(string, indicator), bypassing dynamic row/date conversion.
 * Its row view retains the existing hydration and numeric checks without
 * allocating unbound soci::values holders, whose lifetime SOCI cannot manage.
 */
class TypedResultStorage
{
    using Storage = std::variant<int, long long, unsigned long long, double, std::string>;

    struct Buffer
    {
        ResultColumn column;
        Storage value;
        soci::indicator indicator = soci::i_null;
    };

    std::vector<std::unique_ptr<Buffer>> buffers;

    auto find(std::string_view alias) const -> const Buffer&
    {
        const auto found =
            std::ranges::find_if(buffers, [&](const auto& buffer) { return buffer->column.alias == alias; });
        if (found == buffers.end())
            throw ConversionError{"The requested result alias is missing"};
        return **found;
    }

    struct NoResultValidation
    {
        auto operator()(const BoundValue&) const noexcept -> void {}
    };

    static auto decodedResult(model::ColumnType type, std::string_view text) -> query::QueryValue::Value
    {
        using enum model::ColumnType;
        switch (type)
        {
        case Date:
            return decodeScalarValue<orm::Date>(text);
        case TimeOfDay:
            return decodeScalarValue<orm::TimeOfDay>(text);
        case LocalDateTime:
            return decodeScalarValue<orm::LocalDateTime>(text);
        case UtcTimestamp:
            return decodeScalarValue<orm::UtcTimestamp>(text);
        case Uuid:
            return decodeScalarValue<orm::Uuid>(text);
        default:
            throw ConversionError{"The result column does not use encoded scalar storage"};
        }
    }

    static auto storageFor(model::ColumnType type) -> Storage
    {
        using enum model::ColumnType;
        switch (type)
        {
        case Bool:
        case Char:
        case UnsignedChar:
        case Short:
        case UnsignedShort:
        case Int:
            return int{};
        case UnsignedInt:
        case UnsignedLongLong:
            return static_cast<unsigned long long>(0);
        case LongLong:
            return static_cast<long long>(0);
        case Float:
        case Double:
            return double{};
        case String:
        case Date:
        case TimeOfDay:
        case LocalDateTime:
        case UtcTimestamp:
        case Uuid:
            return std::string{};
        }
        throw ConversionError{"Unsupported result column type"};
    }

public:
    struct Properties
    {
        decltype(soci::dt_string) type;
        auto get_data_type() const noexcept -> decltype(soci::dt_string)
        {
            return type;
        }
    };

    auto get_indicator(std::string_view alias) const -> soci::indicator
    {
        return find(alias).indicator;
    }

    auto get_properties(std::string_view alias) const -> Properties
    {
        return std::visit(
            [](const auto& value) -> Properties
            {
                using T = std::remove_cvref_t<decltype(value)>;
                if constexpr (std::is_same_v<T, int>)
                    return {soci::dt_integer};
                else if constexpr (std::is_same_v<T, long long>)
                    return {soci::dt_long_long};
                else if constexpr (std::is_same_v<T, unsigned long long>)
                    return {soci::dt_unsigned_long_long};
                else if constexpr (std::is_same_v<T, double>)
                    return {soci::dt_double};
                else
                    return {soci::dt_string};
            },
            find(alias).value);
    }

    template <typename T>
    auto get(std::string_view alias) const -> T
    {
        const auto& buffer = find(alias);
        if (buffer.indicator == soci::i_null)
            throw ConversionError{"A non-null result field contains NULL"};
        const auto* value = std::get_if<T>(&buffer.value);
        if (!value)
            throw ConversionError{"The result field has a different storage representation"};
        return *value;
    }

    explicit TypedResultStorage(std::vector<ResultColumn> columns)
    {
        buffers.reserve(columns.size());
        for (auto& column : columns)
        {
            if (column.requestedType &&
                (isEncodedResultType(column.logicalType) || isEncodedResultType(*column.requestedType)) &&
                column.logicalType != *column.requestedType)
                throw ConversionError{"A projected scalar type does not match its requested result field"};
            auto storage = storageFor(column.logicalType);
            buffers.push_back(std::make_unique<Buffer>(Buffer{std::move(column), std::move(storage), soci::i_null}));
        }
    }

    template <typename Consume, typename Validate = NoResultValidation>
    auto read(soci::session& session, std::string_view sql, soci::values& parameters, Consume consume,
              std::optional<BackendType> backend = std::nullopt, Validate validate = {}) -> void
    {
        const bool signedSqlIntegers =
            backend && (*backend == BackendType::Postgres || *backend == BackendType::Sqlite);
        // Preserve the SQL sign until checkedNumericCast runs; an unsigned
        // SOCI exchange can turn -1 into the maximum unsigned value.
        for (auto& buffer : buffers)
        {
            if (buffer->column.logicalType == model::ColumnType::UnsignedInt ||
                buffer->column.logicalType == model::ColumnType::UnsignedLongLong)
                buffer->value = signedSqlIntegers ? Storage{static_cast<long long>(0)} :
                                                    Storage{static_cast<unsigned long long>(0)};
        }
        soci::statement statement{session};
        statement.alloc();
        statement.prepare(std::string{sql});
        statement.exchange(soci::use(parameters));
        for (auto& buffer : buffers)
        {
            std::visit([&](auto& value) { statement.exchange(soci::into(value, buffer->indicator)); }, buffer->value);
        }
        statement.define_and_bind();
        statement.execute();
        while (statement.fetch())
        {
            for (const auto& buffer : buffers)
            {
                if (buffer->indicator != soci::i_ok && buffer->indicator != soci::i_null)
                    throw ConversionError{"A database result was truncated by its transport"};
                if (buffer->indicator != soci::i_null && isEncodedResultType(buffer->column.logicalType))
                {
                    auto value = decodedResult(buffer->column.logicalType, std::get<std::string>(buffer->value));
                    if (backend)
                    {
                        try
                        {
                            std::visit(
                                [&](const auto& scalar)
                                {
                                    if constexpr (SociEncodedValue<std::remove_cvref_t<decltype(scalar)>>)
                                        orm::detail::values::validateNative(scalar, *backend);
                                },
                                value);
                        }
                        catch (const std::invalid_argument&)
                        {
                            throw ConversionError{"A database result exceeds its native scalar type range"};
                        }
                    }
                    validate(BoundValue{.logicalType = buffer->column.logicalType, .value = std::move(value)});
                }
            }
            consume(*this);
        }
    }
};
} // namespace orm::db::binding
