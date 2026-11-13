module;

#include <algorithm>
#include <array>
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
/**
 * A session-bound, stable exchange buffer for one Binary16 parameter or result.
 * The buffer and its indicator must outlive the statement using them. SQLite
 * results additionally require typeof(value) from the same row, because SOCI's
 * blob exchange converts other storage classes before returning the bytes.
 */
class Binary16Buffer
{
    soci::blob blob_;
    soci::indicator indicator_{soci::i_null};

public:
    explicit Binary16Buffer(soci::session& session) : blob_{session} {}

    auto set(const std::optional<orm::Uuid>& value) -> void
    {
        indicator_ = value ? soci::i_ok : soci::i_null;
        blob_.trim(0);
        if (value)
        {
            // SOCI's blob API takes `const char*`; `std::byte` carries the same
            // representation and is the type the UUID codec exposes.
            const auto* bytes = reinterpret_cast<const char*>(value->bytes().data());
            if (blob_.write_from_start(bytes, value->bytes().size()) != value->bytes().size())
                throw ConversionError{"Could not write a complete Binary16 value"};
        }
    }

    auto exchangeUse(soci::statement& statement, const std::string& name) -> void
    {
        statement.exchange(soci::use(blob_, indicator_, name));
    }

    auto exchangeInto(soci::statement& statement) -> void
    {
        statement.exchange(soci::into(blob_, indicator_));
    }

    [[nodiscard]] auto indicator() const noexcept -> soci::indicator
    {
        return indicator_;
    }

    [[nodiscard]] auto read(BackendType backend, std::string_view sqliteStorageClass = {}) -> std::optional<orm::Uuid>
    {
        if (backend != BackendType::Sqlite && backend != BackendType::Mysql)
            throw ConversionError{"Binary16 is unavailable for this backend"};
        if (indicator_ == soci::i_null)
            return std::nullopt;
        if (indicator_ != soci::i_ok)
            throw ConversionError{"A Binary16 result was truncated by its transport"};
        if (backend == BackendType::Sqlite && sqliteStorageClass != "blob")
            throw ConversionError{"A Binary16 result is not a SQLite BLOB"};
        std::array<std::byte, 16> bytes{};
        auto* buffer = reinterpret_cast<char*>(bytes.data());
        if (blob_.get_len() != bytes.size() || blob_.read_from_start(buffer, bytes.size()) != bytes.size())
            throw ConversionError{"A Binary16 result must contain exactly 16 bytes"};
        return orm::Uuid{bytes};
    }
};

/** Owns the regular SOCI values and session-bound Binary16 input buffers. */
class StatementBindings
{
    struct BinaryParameter
    {
        std::string name;
        Binary16Buffer buffer;
        const BackendRuntime& runtime;

        BinaryParameter(std::string nameInit, soci::session& session, const BackendRuntime& runtimeInit)
            : name{std::move(nameInit)}, buffer{session}, runtime{runtimeInit}
        {
        }
    };

    soci::values values_;
    soci::values* borrowedValues_{};
    std::vector<std::unique_ptr<BinaryParameter>> binaries_;

public:
    StatementBindings() = default;
    explicit StatementBindings(soci::values& values) : borrowedValues_{&values} {}

    auto add(soci::session& session, const BackendRuntime& runtime, std::string_view name,
             const BoundValue& bound) -> void
    {
        if (bound.uuidStorage == model::UuidStorage::Native)
        {
            runtime.bind(values(), name, bound);
            return;
        }
        if (bound.uuidStorage == model::UuidStorage::Text36)
        {
            if (bound.logicalType != model::ColumnType::Uuid)
                throw ConversionError{"Text36 requires a logical UUID parameter"};
            BoundValue text{.logicalType = model::ColumnType::String, .value = std::nullopt};
            if (bound.value)
            {
                const auto* uuid = std::get_if<orm::Uuid>(&*bound.value);
                if (!uuid)
                    throw ConversionError{"A Text36 parameter has incompatible storage"};
                text.value = orm::detail::values::format(*uuid);
            }
            runtime.bind(values(), name, text);
            return;
        }
        if (bound.uuidStorage != model::UuidStorage::Binary16)
            throw ConversionError{"Invalid UUID storage representation"};
        if (bound.logicalType != model::ColumnType::Uuid)
            throw ConversionError{"Binary16 requires a logical UUID parameter"};

        std::optional<orm::Uuid> value;
        if (bound.value)
        {
            const auto* uuid = std::get_if<orm::Uuid>(&*bound.value);
            if (!uuid)
                throw ConversionError{"A Binary16 parameter has incompatible storage"};
            value = *uuid;
        }
        auto binary = std::make_unique<BinaryParameter>(std::string{name}, session, runtime);
        binary->buffer.set(value);
        binaries_.push_back(std::move(binary));
    }

    auto add(soci::session& session, const BackendRuntime& runtime, const StatementParameter& parameter) -> void
    {
        add(session, runtime, parameter.name, parameter.getBoundValue());
    }

    [[nodiscard]] auto hasBinary() const noexcept -> bool
    {
        return !binaries_.empty();
    }

    auto exchange(soci::statement& statement) -> void
    {
        statement.exchange(soci::use(values()));
        for (auto& binary : binaries_)
            if (binary->runtime.hasStatementParameter(statement, binary->name))
                binary->buffer.exchangeUse(statement, binary->name);
    }

    auto values() noexcept -> soci::values&
    {
        return borrowedValues_ ? *borrowedValues_ : values_;
    }
};

struct ResultColumn
{
    std::string alias;
    model::ColumnType logicalType;
    std::optional<model::ColumnType> requestedType = std::nullopt;
    const model::ColumnView* column = nullptr;
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

inline auto validateResultIndicator(soci::indicator indicator) -> void
{
    if (indicator != soci::i_ok && indicator != soci::i_null)
        throw ConversionError{"A database result was truncated by its transport"};
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
            result.push_back({std::string{model->tableName} + "_" + std::string{column.name}, column.type.value(),
                              std::nullopt, &column});
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
                    result.push_back(
                        {prefix + "_" + std::string{child.name}, child.type.value(), std::nullopt, &child});
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
        {
            result.push_back({std::string{projection.alias}, resultSourceType(projection.source, model)});
            if (result.back().logicalType == model::ColumnType::Uuid)
                result.back().column = db::detail::resolveSqlColumn(projection.source, model).column;
        }
        else
        {
            const auto source = std::visit([](const auto& value) { return resultSource(value); }, projection.source);
            result.push_back({projection.resultField, resultSourceType(source, model)});
            if (result.back().logicalType == model::ColumnType::Uuid)
                result.back().column = db::detail::resolveSqlColumn(source, model).column;
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
        std::unique_ptr<Binary16Buffer> binary;
        std::string sqliteStorageClass;
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
        // read() calls this only after isEncodedResultType accepts the logical type.
        if (type == Date)
            return decodeScalarValue<orm::Date>(text);
        if (type == TimeOfDay)
            return decodeScalarValue<orm::TimeOfDay>(text);
        if (type == LocalDateTime)
            return decodeScalarValue<orm::LocalDateTime>(text);
        if (type == UtcTimestamp)
            return decodeScalarValue<orm::UtcTimestamp>(text);
        return decodeScalarValue<orm::Uuid>(text);
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
            buffers.push_back(
                std::make_unique<Buffer>(Buffer{std::move(column), std::move(storage), soci::i_null, nullptr, {}}));
        }
    }

    template <typename Consume, typename Validate = NoResultValidation>
    auto read(soci::session& session, std::string_view sql, StatementBindings& parameters, Consume consume,
              std::optional<BackendType> backend = std::nullopt, Validate validate = {}) -> void
    {
        const bool signedSqlIntegers =
            backend && (*backend == BackendType::Postgres || *backend == BackendType::Sqlite);
        // Preserve the SQL sign until checkedNumericCast runs; an unsigned
        // SOCI exchange can turn -1 into the maximum unsigned value.
        for (auto& buffer : buffers)
        {
            buffer->binary.reset();
            buffer->sqliteStorageClass.clear();
            if (buffer->column.logicalType == model::ColumnType::UnsignedInt ||
                buffer->column.logicalType == model::ColumnType::UnsignedLongLong)
                buffer->value = signedSqlIntegers ? Storage{static_cast<long long>(0)} :
                                                    Storage{static_cast<unsigned long long>(0)};
            if (backend && buffer->column.column && buffer->column.logicalType == model::ColumnType::Uuid &&
                buffer->column.column->uuidStorage(*backend) == model::UuidStorage::Binary16)
                buffer->binary = std::make_unique<Binary16Buffer>(session);
        }
        soci::statement statement{session};
        statement.alloc();
        statement.prepare(std::string{sql});
        parameters.exchange(statement);
        for (auto& buffer : buffers)
        {
            if (buffer->binary)
                buffer->binary->exchangeInto(statement);
            else
                std::visit([&](auto& value) { statement.exchange(soci::into(value, buffer->indicator)); },
                           buffer->value);
        }
        if (backend && *backend == BackendType::Sqlite)
            for (auto& buffer : buffers)
                if (buffer->binary || (buffer->column.column && buffer->column.logicalType == model::ColumnType::Uuid &&
                                       buffer->column.column->uuidStorage(*backend) == model::UuidStorage::Text36))
                    statement.exchange(soci::into(buffer->sqliteStorageClass));
        statement.define_and_bind();
        statement.execute();
        while (statement.fetch())
        {
            for (const auto& buffer : buffers)
            {
                // A Text36 column stores a canonical TEXT UUID, so the native UUID
                // validation (Postgres/MariaDB only) must not apply to its result.
                const bool text36Result = backend && buffer->column.logicalType == model::ColumnType::Uuid &&
                                          buffer->column.column != nullptr &&
                                          buffer->column.column->uuidStorage(*backend) == model::UuidStorage::Text36;
                if (buffer->binary)
                {
                    const auto value = buffer->binary->read(*backend, buffer->sqliteStorageClass);
                    buffer->indicator = buffer->binary->indicator();
                    if (value)
                        buffer->value = value->toString();
                }
                else if (backend && *backend == BackendType::Sqlite &&
                         buffer->column.logicalType == model::ColumnType::Uuid && buffer->column.column != nullptr &&
                         buffer->column.column->uuidStorage(*backend) == model::UuidStorage::Text36 &&
                         buffer->indicator != soci::i_null && buffer->sqliteStorageClass != "text")
                {
                    throw ConversionError{"A Text36 result is not a SQLite TEXT value"};
                }
                validateResultIndicator(buffer->indicator);
                if (buffer->indicator != soci::i_null && isEncodedResultType(buffer->column.logicalType))
                {
                    auto value = decodedResult(buffer->column.logicalType, std::get<std::string>(buffer->value));
                    if (backend && !buffer->binary && !text36Result)
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
                    validate(BoundValue{.logicalType = buffer->column.logicalType,
                                        .value = std::move(value),
                                        .uuidStorage = buffer->binary ?
                                                           model::UuidStorage::Binary16 :
                                                           (backend && buffer->column.column != nullptr &&
                                                                    buffer->column.column->uuidStorage(*backend) ==
                                                                        model::UuidStorage::Text36 ?
                                                                model::UuidStorage::Text36 :
                                                                model::UuidStorage::Native)});
                }
            }
            consume(*this);
        }
    }

    template <typename Consume, typename Validate = NoResultValidation>
    auto read(soci::session& session, std::string_view sql, soci::values& parameters, Consume consume,
              std::optional<BackendType> backend = std::nullopt, Validate validate = {}) -> void
    {
        StatementBindings bindings{parameters};
        read(session, sql, bindings, std::move(consume), backend, std::move(validate));
    }
};
} // namespace orm::db::binding
