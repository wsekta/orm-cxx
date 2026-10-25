module;

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "ConnectionParameters.hpp"
#include "NativeApi.hpp"
#include "soci/mysql/soci-mysql.h"
#include "soci/soci.h"
#include "TemporalProfile.hpp"

module orm;

import :internal;
import :connection_config;
import :value_codec;

namespace orm::db::mysql_family
{
auto isEmptyScript(std::string_view sql) -> bool
{
    for (std::size_t i = 0; i < sql.size();)
    {
        if (std::isspace(static_cast<unsigned char>(sql[i])) || sql[i] == ';')
        {
            ++i;
        }
        else if (sql[i] == '#' || (sql[i] == '-' && i + 2 < sql.size() && sql[i + 1] == '-' &&
                                   std::isspace(static_cast<unsigned char>(sql[i + 2]))))
        {
            const auto end = sql.find_first_of("\r\n", i);
            i = end == std::string_view::npos ? sql.size() : end + 1;
        }
        else if (sql[i] == '/' && i + 1 < sql.size() && sql[i + 1] == '*')
        {
            if (i + 2 < sql.size() &&
                (sql[i + 2] == '!' || (sql[i + 2] == 'M' && i + 3 < sql.size() && sql[i + 3] == '!')))
            {
                return false;
            }
            const auto end = sql.find("*/", i + 2);
            if (end == std::string_view::npos)
            {
                return false;
            }
            i = end + 2;
        }
        else
        {
            return false;
        }
    }
    return true;
}

auto tokenizeStatement(std::string_view sql) -> StatementTokens
{
    StatementTokens result{{""}, {}};
    for (std::size_t i = 0; i < sql.size();)
    {
        const auto begin = i;
        const auto c = sql[i];
        if (c == '\'' || c == '"' || c == '`')
        {
            ++i;
            bool closed = false;
            while (i < sql.size())
            {
                if (sql[i] == '\\' && c != '`')
                {
                    i += std::min<std::size_t>(2, sql.size() - i);
                }
                else if (sql[i] == c)
                {
                    ++i;
                    if (i < sql.size() && sql[i] == c)
                    {
                        ++i;
                    }
                    else
                    {
                        closed = true;
                        break;
                    }
                }
                else
                {
                    ++i;
                }
            }
            if (!closed)
            {
                throw std::invalid_argument{"Unterminated MySQL-family SQL quote"};
            }
        }
        else if (c == '#' || (c == '-' && i + 2 < sql.size() && sql[i + 1] == '-' &&
                              std::isspace(static_cast<unsigned char>(sql[i + 2]))))
        {
            const auto end = sql.find_first_of("\r\n", i);
            i = end == std::string_view::npos ? sql.size() : end + 1;
        }
        else if (c == '/' && i + 1 < sql.size() && sql[i + 1] == '*')
        {
            // Executable comments are server SQL, not an inert place for bind markers.
            if (i + 2 < sql.size() &&
                (sql[i + 2] == '!' || (sql[i + 2] == 'M' && i + 3 < sql.size() && sql[i + 3] == '!')))
            {
                throw std::invalid_argument{"Executable SQL comments are not supported in bound statements"};
            }
            const auto end = sql.find("*/", i + 2);
            if (end == std::string_view::npos)
            {
                throw std::invalid_argument{"Unterminated MySQL-family SQL comment"};
            }
            i = end + 2;
        }
        else if (c == ':' && i + 1 < sql.size() && sql[i + 1] != '=')
        {
            ++i;
            const auto nameBegin = i;
            while (i < sql.size() && (std::isalnum(static_cast<unsigned char>(sql[i])) || sql[i] == '_'))
            {
                ++i;
            }
            const auto name = sql.substr(nameBegin, i - nameBegin);
            if (!detail::isPortableBindName(name))
            {
                throw std::invalid_argument{"Invalid MySQL-family bind marker"};
            }
            result.names.emplace_back(name);
            result.chunks.emplace_back();
            continue;
        }
        else
        {
            ++i;
        }
        result.chunks.back().append(sql.substr(begin, i - begin));
    }
    return result;
}

auto sqlType(model::ColumnType type, std::optional<std::size_t> maxStringLength) -> std::string
{
    using enum model::ColumnType;
    switch (type)
    {
    case Bool:
        return "TINYINT";
    case Char:
        return "SMALLINT";
    case UnsignedChar:
        return "TINYINT UNSIGNED";
    case Short:
        return "SMALLINT";
    case UnsignedShort:
        return "SMALLINT UNSIGNED";
    case Int:
        return "INT";
    case UnsignedInt:
        return "INT UNSIGNED";
    case LongLong:
        return "BIGINT";
    case UnsignedLongLong:
        return "BIGINT UNSIGNED";
    case Float:
    case Double:
        return "DOUBLE";
    case String:
        if (maxStringLength)
        {
            if (*maxStringLength == 0 || *maxStringLength > 16'383)
            {
                throw std::invalid_argument{"MySQL-family VARCHAR length must be between 1 and 16383 characters"};
            }
            return std::format("VARCHAR({})", *maxStringLength);
        }
        return "LONGTEXT";
    case Date:
        return "DATE";
    case TimeOfDay:
        return "TIME(6)";
    case LocalDateTime:
        return "DATETIME(6)";
    case UtcTimestamp:
        return "TIMESTAMP(6)";
    case Uuid:
        break;
    }
    throw std::runtime_error{"Unsupported MySQL-family column type"};
}

auto renderInsert(const InsertIfAbsentSpec& insert) -> std::string
{
    if (insert.columns.empty() || insert.columns.size() != insert.valueExpressions.size() ||
        insert.conflictColumns.empty())
    {
        throw std::invalid_argument{"Insert-if-absent requires matching columns, values and a conflict key"};
    }
    std::string columns;
    std::string values;
    for (std::size_t i = 0; i < insert.columns.size(); ++i)
    {
        if (i != 0)
        {
            columns += ", ";
            values += ", ";
        }
        columns += detail::quoteStandardIdentifier(insert.columns[i], CompiledSqlFlavor::MySQLFamily);
        values += insert.valueExpressions[i];
    }
    for (const auto& column : insert.conflictColumns)
    {
        if (std::ranges::find(insert.columns, column) == insert.columns.end())
        {
            throw std::invalid_argument{"Conflict columns must belong to the inserted columns"};
        }
    }
    return std::format("INSERT INTO {} ({}) VALUES ({});",
                       detail::quoteStandardIdentifier(insert.tableName, CompiledSqlFlavor::MySQLFamily), columns,
                       values);
}

auto capabilities() -> BackendCapabilities
{
    using enum model::ColumnType;
    BackendCapabilities result;
    result.schema.createTableIfNotExists = true;
    result.schema.dropTableIfExists = true;
    result.schema.autoIncrementPrimaryKey = true;
    result.schema.compositePrimaryKeys = true;
    result.schema.foreignKeys = true;
    result.schema.onDeleteCascade = true;
    result.schema.transactionalDdl = false;
    result.schema.boundedStringKeys = true;
    result.query.limit = true;
    result.query.offset = true;
    result.query.offsetWithoutLimit = true;
    result.query.projections = true;
    result.query.groupBy = true;
    result.query.having = true;
    result.query.collectionPredicates = true;
    result.query.distinctOrderByRequiresProjectedColumn = true;
    result.query.strictProjectionGrouping = true;
    result.mutations.insert = true;
    result.mutations.update = true;
    result.mutations.remove = true;
    result.mutations.atomicInsertIfAbsent = true;
    result.mutations.affectedRows = AffectedRowsSupport::Reliable;
    result.relations = {true, true, true, true, true, true, true};
    result.migrations = {true, MigrationAtomicity::NonTransactional};
    result.supportedColumnTypes = {Bool,        Char,      UnsignedChar,     Short,       UnsignedShort, Int,
                                   UnsignedInt, LongLong,  UnsignedLongLong, Float,       Double,        String,
                                   Date,        TimeOfDay, LocalDateTime,    UtcTimestamp};
    result.valueLimits.minDate = orm::Date{std::chrono::year{1000}, std::chrono::month{1}, std::chrono::day{1}};
    result.valueLimits.maxDate = orm::Date{std::chrono::year{9999}, std::chrono::month{12}, std::chrono::day{31}};
    result.valueLimits.minLocalDateTime = orm::LocalDateTime{std::chrono::local_days{*result.valueLimits.minDate}};
    result.valueLimits.maxLocalDateTime = orm::LocalDateTime{std::chrono::local_days{*result.valueLimits.maxDate}} +
                                          std::chrono::hours{24} - std::chrono::microseconds{500001};
    result.valueLimits.minUtcTimestamp = orm::UtcTimestamp{std::chrono::seconds{1}};
    result.valueLimits.maxUtcTimestamp =
        orm::UtcTimestamp{std::chrono::seconds{2'147'483'647}} + std::chrono::microseconds{499999};
    result.transactions = true;
    return result;
}

namespace
{
template <typename Backend, typename Type>
auto describedColumnType(void (Backend::*)(int, Type&, std::string&)) -> Type;
using DescribedColumnType = decltype(describedColumnType(&soci::details::statement_backend::describe_column));

template <typename Backend>
auto synchronizeBindingFlags(Backend& native, const Backend& wrapper) -> void
{
    // Newer SOCI versions set these flags on the public backend rather than in the native binding factories.
    if constexpr (requires { wrapper.hasUseElements_; })
    {
        native.hasIntoElements_ = wrapper.hasIntoElements_;
        native.hasVectorIntoElements_ = wrapper.hasVectorIntoElements_;
        native.hasUseElements_ = wrapper.hasUseElements_;
        native.hasVectorUseElements_ = wrapper.hasVectorUseElements_;
    }
}

template <typename Backend, typename Type>
class OptionalStatementHooks : public Backend
{
protected:
    [[nodiscard]] virtual auto nativeBackend() const -> const Backend& = 0;
};

template <typename Backend, typename Type>
    requires requires(const Backend& backend, Type type) {
        backend.to_data_type(type);
        backend.exchange_dbtype_for(type);
        backend.get_row_to_dump();
    }
class OptionalStatementHooks<Backend, Type> : public Backend
{
public:
    auto to_data_type(Type type) const -> soci::data_type override
    {
        return nativeBackend().to_data_type(type);
    }
    auto exchange_dbtype_for(Type type) const -> Type override
    {
        return nativeBackend().exchange_dbtype_for(type);
    }
    auto get_row_to_dump() const -> int override
    {
        return nativeBackend().get_row_to_dump();
    }

protected:
    [[nodiscard]] virtual auto nativeBackend() const -> const Backend& = 0;
};

class StatementBackend final : public OptionalStatementHooks<soci::details::statement_backend, DescribedColumnType>
{
public:
    explicit StatementBackend(soci::details::session_backend& session)
        : native_{session.make_statement_backend()}, nativeSession_{static_cast<soci::mysql_session_backend&>(session)}
    {
    }
    auto alloc() -> void override
    {
        native_->alloc();
    }
    auto clean_up() -> void override
    {
        native_->clean_up();
        affectedRows_ = -1;
    }
    auto prepare(const std::string& sql, soci::details::statement_type /*type*/) -> void override
    {
        auto tokens = tokenizeStatement(sql);
        auto& statement = *static_cast<soci::mysql_statement_backend*>(native_.get());
        statement.queryChunks_ = std::move(tokens.chunks);
        statement.names_ = std::move(tokens.names);
        affectedRows_ = -1;
    }
    auto execute(int number) -> exec_fetch_result override
    {
        synchronizeBindingFlags(*native_, static_cast<const soci::details::statement_backend&>(*this));
        const bool alreadyExecuted = static_cast<soci::mysql_statement_backend*>(native_.get())->justDescribed_;
        if (!alreadyExecuted)
            beforeStatement();
        const auto result = native_->execute(number);
        // MySQL stores this count on the connection. Even a later mysql_ping() resets it.
        if (!alreadyExecuted || affectedRows_ < 0)
            affectedRows_ = native_->get_affected_rows();
        return result;
    }
    auto fetch(int number) -> exec_fetch_result override
    {
        return native_->fetch(number);
    }
    auto get_affected_rows() -> long long override
    {
        return affectedRows_;
    }
    auto get_number_of_rows() -> int override
    {
        return native_->get_number_of_rows();
    }
    auto get_parameter_name(int index) const -> std::string override
    {
        return nativeBackend().get_parameter_name(index);
    }
    auto rewrite_for_procedure_call(const std::string& sql) -> std::string override
    {
        return native_->rewrite_for_procedure_call(sql);
    }
    auto prepare_for_describe() -> int override
    {
        synchronizeBindingFlags(*native_, static_cast<const soci::details::statement_backend&>(*this));
        beforeStatement();
        const auto count = native_->prepare_for_describe();
        affectedRows_ = native_->get_affected_rows();
        return count;
    }
    auto describe_column(int column, DescribedColumnType& type, std::string& name) -> void override
    {
        native_->describe_column(column, type, name);
    }
    auto make_into_type_backend() -> soci::details::standard_into_type_backend* override
    {
        return native_->make_into_type_backend();
    }
    auto make_use_type_backend() -> soci::details::standard_use_type_backend* override
    {
        return native_->make_use_type_backend();
    }
    auto make_vector_into_type_backend() -> soci::details::vector_into_type_backend* override
    {
        return native_->make_vector_into_type_backend();
    }
    auto make_vector_use_type_backend() -> soci::details::vector_use_type_backend* override
    {
        return native_->make_vector_use_type_backend();
    }

private:
    auto beforeStatement() const -> void
    {
        const auto& chunks = static_cast<soci::mysql_statement_backend*>(native_.get())->queryChunks_;
        if (chunks.empty())
            throw soci::soci_error{"MySQL-family statement must be prepared before execution"};
        orm_mysql_native::calls().beforeStatement(nativeSession_.conn_, chunks.front());
    }
    [[nodiscard]] auto nativeBackend() const -> const soci::details::statement_backend& override
    {
        return *native_;
    }
    std::unique_ptr<soci::details::statement_backend> native_;
    soci::mysql_session_backend& nativeSession_;
    long long affectedRows_ = -1;
};

class SessionBackend final : public soci::details::session_backend
{
public:
    explicit SessionBackend(const soci::connection_parameters& parameters)
        : native_{soci::factory_mysql()->make_session(parameters)}
    {
    }
    [[nodiscard]] auto connectionHandle() const noexcept -> MYSQL*
    {
        return static_cast<soci::mysql_session_backend*>(native_.get())->conn_;
    }
    auto setIndexByteLimit(std::size_t value) -> void
    {
        maxIndexBytes_ = value;
    }
    [[nodiscard]] auto indexByteLimit() const noexcept -> std::optional<std::size_t>
    {
        return maxIndexBytes_;
    }
    auto setUtcTimestampLimit(orm::UtcTimestamp value) -> void
    {
        maxUtcTimestamp_ = value;
    }
    [[nodiscard]] auto utcTimestampLimit() const noexcept -> std::optional<orm::UtcTimestamp>
    {
        return maxUtcTimestamp_;
    }
    auto is_connected() -> bool override
    {
        return connectionHandle() != nullptr && native_->is_connected();
    }
    auto begin() -> void override
    {
        native_->begin();
        transactionActive_ = true;
    }
    auto commit() -> void override
    {
        native_->commit();
        finishLinkLocks();
    }
    auto rollback() -> void override
    {
        if (connectionHandle() != nullptr)
        {
            try
            {
                native_->rollback();
            }
            catch (...)
            {
                clean_up();
                throw;
            }
        }
        finishLinkLocks();
    }
    auto clean_up() -> void
    {
        auto& native = *static_cast<soci::mysql_session_backend*>(native_.get());
        if (native.conn_ != nullptr)
        {
            orm_mysql_native::calls().close(native.conn_);
            native.conn_ = nullptr;
        }
        transactionActive_ = false;
        linkLocks_.clear();
    }
    auto make_statement_backend() -> StatementBackend* override
    {
        return new StatementBackend{*native_};
    }
    auto get_last_insert_id(soci::session& session, const std::string& table, long long& value) -> bool override
    {
        return native_->get_last_insert_id(session, table, value);
    }
    auto get_dummy_from_table() const -> std::string override
    {
        return native_->get_dummy_from_table();
    }
    auto get_backend_name() const -> std::string override
    {
        return native_->get_backend_name();
    }
    auto get_table_names_query() const -> std::string override
    {
        return native_->get_table_names_query();
    }
    auto make_rowid_backend() -> soci::details::rowid_backend* override
    {
        return native_->make_rowid_backend();
    }
    auto make_blob_backend() -> soci::details::blob_backend* override
    {
        return native_->make_blob_backend();
    }
    [[nodiscard]] auto ownsLinkLock(const std::string& name) const -> bool
    {
        return std::ranges::find(linkLocks_, name) != linkLocks_.end();
    }
    auto rememberLinkLock(std::string name) -> void
    {
        linkLocks_.push_back(std::move(name));
    }
    [[nodiscard]] auto transactionActive() const noexcept -> bool
    {
        return transactionActive_;
    }
    auto releaseLinkLock(const std::string& name) noexcept -> void
    {
        auto* connection = connectionHandle();
        if (connection != nullptr)
        {
            // Names contain only a fixed ASCII prefix and a SHA-256 fragment.
            const auto query = "SELECT RELEASE_LOCK('" + name + "')";
            if (orm_mysql_native::calls().realQuery(connection, query.data(),
                                                    static_cast<unsigned long>(query.size())) != 0)
            {
                clean_up();
                return;
            }
            auto* result = orm_mysql_native::calls().storeResult(connection);
            const auto row = result != nullptr ? orm_mysql_native::calls().fetchRow(result) : nullptr;
            const bool released = row != nullptr && row[0] != nullptr && std::string_view{row[0]} == "1";
            if (result != nullptr)
                orm_mysql_native::calls().freeResult(result);
            if (!released)
            {
                clean_up();
                return;
            }
        }
        std::erase(linkLocks_, name);
    }

private:
    auto finishLinkLocks() noexcept -> void
    {
        transactionActive_ = false;
        while (!linkLocks_.empty())
        {
            const auto name = linkLocks_.back();
            releaseLinkLock(name);
        }
    }
    bool transactionActive_ = false;
    std::optional<std::size_t> maxIndexBytes_;
    std::optional<orm::UtcTimestamp> maxUtcTimestamp_;
    std::vector<std::string> linkLocks_;
    std::unique_ptr<soci::details::session_backend> native_;
};

class LinkLockError final : public soci::soci_error
{
public:
    explicit LinkLockError(std::optional<unsigned int> native = std::nullopt)
        : soci_error{"Could not acquire the relation link lock"}, nativeCode{native}
    {
    }
    [[nodiscard]] auto get_error_category() const -> error_category override
    {
        return unknown_transaction_state;
    }
    const std::optional<unsigned int> nativeCode;
};

class LinkLockScope
{
public:
    LinkLockScope(SessionBackend& session, std::string name)
        : session_{session}, name_{std::move(name)}, release_{!session.transactionActive()}
    {
    }
    ~LinkLockScope()
    {
        if (release_)
            session_.releaseLinkLock(name_);
    }
    LinkLockScope(const LinkLockScope&) = delete;
    auto operator=(const LinkLockScope&) -> LinkLockScope& = delete;

private:
    SessionBackend& session_;
    std::string name_;
    bool release_;
};

class Factory final : public soci::backend_factory
{
public:
    auto make_session(const soci::connection_parameters& parameters) const -> SessionBackend* override
    {
        return new SessionBackend{parameters};
    }
};

class Runtime final : public BackendRuntime
{
public:
    explicit Runtime(BackendType type) : type_{type} {}
    auto open(soci::session& session, ConnectionConfigView config) const -> void override
    {
        std::string arguments;
        try
        {
            if (type_ == BackendType::Mysql)
                arguments = orm_mysql_connection::serialize(config.get<MysqlConfig>());
            else
                arguments = orm_mysql_connection::serialize(config.get<MariadbConfig>());
        }
        catch (const std::invalid_argument&)
        {
            throw DatabaseError{DatabaseErrorCode::Connection, type_, "connect",
                                "Invalid MySQL-family connection configuration"};
        }
        // No reconnect option is exposed: transactions and locks must never be silently lost.
        static const Factory factory;
        session.open(factory, arguments);
    }
    auto onConnect(soci::session& session) const -> void override
    {
        std::string version;
        session << "SELECT VERSION()", soci::into(version);
        const bool maria = version.find("MariaDB") != std::string::npos;
        if (maria != (type_ == BackendType::Mariadb))
        {
            throw std::invalid_argument{"Connected server does not match the selected backend"};
        }
        const auto [maxTimestampSeconds, maxTimestampFraction] = orm_mysql_temporal::timestampMaximum(version, maria);
        static_cast<SessionBackend*>(session.get_backend())
            ->setUtcTimestampLimit(orm::UtcTimestamp{std::chrono::seconds{maxTimestampSeconds}} +
                                   std::chrono::microseconds{maxTimestampFraction});
        const auto collation = type_ == BackendType::Mariadb ? "utf8mb4_nopad_bin" : "utf8mb4_0900_bin";
        if (orm_mysql_native::calls().setCharacterSet(handle(session), "utf8mb4") != 0)
        {
            throw soci::mysql_soci_error{"Could not select utf8mb4",
                                         static_cast<int>(orm_mysql_native::calls().errorNumber(handle(session)))};
        }
        session << std::string{"SET SESSION collation_connection = '"} + collation + "'";
        session << "SET SESSION sql_mode = "
                   "'STRICT_TRANS_TABLES,ONLY_FULL_GROUP_BY,ERROR_FOR_DIVISION_BY_ZERO,NO_ENGINE_SUBSTITUTION'";
        session << "SET SESSION foreign_key_checks = 1";
        session << "SET SESSION autocommit = 1";
        session << "SET SESSION time_zone = '+00:00'";
        session << "SET SESSION explicit_defaults_for_timestamp = ON";
        if (type_ == BackendType::Mariadb)
            session << "SET SESSION max_sort_length = 65536";
        // This global setting is fixed when the server starts, so querying it per SQL operation is unnecessary.
        int pageSize{};
        session << "SELECT @@innodb_page_size", soci::into(pageSize);
        static_cast<SessionBackend*>(session.get_backend())
            ->setIndexByteLimit(std::min<std::size_t>(3072, (static_cast<std::size_t>(pageSize) / 16) * 3));
    }
    auto tableExists(soci::session& session, std::string_view tableName) const -> bool override
    {
        const std::string name{tableName};
        int exists{};
        session << "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name=:name "
                   "AND table_type='BASE TABLE'",
            soci::use(name, "name"), soci::into(exists);
        return exists != 0;
    }
    auto limits(soci::session& session) const -> BackendRuntimeLimits override
    {
        return {.maxBindParameters = std::nullopt,
                .maxIndexBytes = static_cast<SessionBackend*>(session.get_backend())->indexByteLimit(),
                .minUtcTimestamp = orm::UtcTimestamp{std::chrono::seconds{1}},
                .maxUtcTimestamp = static_cast<SessionBackend*>(session.get_backend())->utcTimestampLimit()};
    }
    auto normalizeAffectedRows(long long value) const -> std::size_t override
    {
        if (value < 0)
        {
            throw std::runtime_error{"MySQL-family affected-row count is unavailable"};
        }
        return static_cast<std::size_t>(value);
    }
    auto statementErrorInvalidatesTransaction(const soci::soci_error& error) const noexcept -> bool override
    {
        const auto* mysqlError = dynamic_cast<const soci::mysql_soci_error*>(&error);
        return error.get_error_category() == soci::soci_error::connection_error ||
               dynamic_cast<const LinkLockError*>(&error) != nullptr ||
               (mysqlError != nullptr && (mysqlError->err_num_ == 1213 || mysqlError->err_num_ == 1205));
    }
    auto bind(soci::values& values, std::string_view name, const BoundValue& value) const -> void override
    {
        if (value.logicalType == model::ColumnType::Date || value.logicalType == model::ColumnType::TimeOfDay ||
            value.logicalType == model::ColumnType::LocalDateTime ||
            value.logicalType == model::ColumnType::UtcTimestamp || value.logicalType == model::ColumnType::Uuid)
        {
            if (value.logicalType == model::ColumnType::Uuid && type_ != BackendType::Mariadb)
                throw DatabaseError{DatabaseErrorCode::UnsupportedFeature, type_, "bind",
                                    "The backend does not support native UUID values"};
            if (not binding::hasCompatibleStorage(value))
                throw binding::ConversionError{"Native value storage does not match its logical column type"};
            if (not value.isNull())
            {
                try
                {
                    std::visit(
                        [this](const auto& stored)
                        {
                            if constexpr (requires { orm::detail::values::validateNative(stored, type_); })
                                orm::detail::values::validateNative(stored, type_);
                        },
                        *value.value);
                }
                catch (const std::invalid_argument&)
                {
                    throw binding::ConversionError{"Value is outside the backend's native type range"};
                }
            }
            if (value.logicalType == model::ColumnType::UtcTimestamp)
            {
                // TIMESTAMP interprets a local literal in the UTC ORM session. PostgreSQL's offset syntax is
                // not accepted by every supported MySQL-family server.
                BoundValue wire{.logicalType = model::ColumnType::String, .value = std::nullopt};
                if (not value.isNull())
                    wire.value = query::QueryValue::Value{
                        orm::detail::values::format(std::get<orm::UtcTimestamp>(*value.value), false)};
                binding::bindBoundValue(values, name, wire);
                return;
            }
        }
        binding::bindBoundValue(values, name, value);
    }
    auto translateError(const soci::soci_error& error, DatabaseErrorCode fallback,
                        std::string_view operation) const -> DatabaseError override
    {
        auto code = fallback;
        std::optional<std::string> native;
        if (error.get_error_category() == soci::soci_error::connection_error)
        {
            code = DatabaseErrorCode::Connection;
        }
        else if (error.get_error_category() == soci::soci_error::constraint_violation)
        {
            code = DatabaseErrorCode::Constraint;
        }
        else if (error.get_error_category() == soci::soci_error::unknown_transaction_state)
        {
            code = DatabaseErrorCode::Transaction;
        }
        if (const auto* mysqlError = dynamic_cast<const soci::mysql_soci_error*>(&error))
        {
            const auto number = mysqlError->err_num_;
            native = std::to_string(number);
            if (number == 1062 || number == 1048 || number == 1451 || number == 1452 || number == 3819 ||
                number == 4025)
                code = DatabaseErrorCode::Constraint;
            else if (number == 1205 || number == 1213)
                code = DatabaseErrorCode::Transaction;
            else if (number == 1045 || number == 1049 || number == 2002 || number == 2003 || number == 2006 ||
                     number == 2013)
                code = DatabaseErrorCode::Connection;
            else if (number == 1264 || number == 1292 || number == 1366 || number == 1406 || number == 1441)
                code = DatabaseErrorCode::Conversion;
        }
        else if (const auto* lockError = dynamic_cast<const LinkLockError*>(&error);
                 lockError != nullptr && lockError->nativeCode)
        {
            native = std::to_string(*lockError->nativeCode);
        }
        return DatabaseError{code, type_, std::string{operation},
                             "Database operation failed: " + std::string{operation}, native};
    }
    auto executeInsertIfAbsent(soci::session& session, StatementView statement,
                               const InsertIfAbsentSpec& spec) const -> std::size_t override
    {
        soci::values values;
        for (const auto& parameter : statement.parameters)
        {
            bind(values, parameter.name, parameter.getBoundValue());
        }
        std::vector<std::string> primaryKey;
        const std::string table{spec.tableName};
        soci::rowset<soci::row> keyColumns =
            (session.prepare << "SELECT COLUMN_NAME FROM information_schema.statistics "
                                "WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME=:table AND INDEX_NAME='PRIMARY' "
                                "ORDER BY SEQ_IN_INDEX",
             soci::use(table, "table"));
        for (const auto& column : keyColumns)
        {
            primaryKey.push_back(column.get<std::string>(0));
        }
        if (!std::ranges::is_permutation(primaryKey, spec.conflictColumns) ||
            spec.columns.size() != spec.valueExpressions.size())
        {
            throw DatabaseError{DatabaseErrorCode::UnsupportedFeature, type_, "link relation",
                                "The junction primary key does not match its relation mapping"};
        }
        auto& connection = *static_cast<SessionBackend*>(session.get_backend());
        const auto lock = "orm:l:" + migrations::detail::sha256(migrationTable(session) + ":" + table).substr(0, 58);
        if (!connection.ownsLinkLock(lock))
        {
            int acquired{};
            soci::indicator indicator{};
            try
            {
                session << "SELECT GET_LOCK(:name,10)", soci::use(lock, "name"), soci::into(acquired, indicator);
            }
            catch (const soci::mysql_soci_error& error)
            {
                if (error.get_error_category() == soci::soci_error::connection_error)
                    throw;
                throw LinkLockError{error.err_num_};
            }
            if (indicator == soci::i_null || acquired != 1)
                throw LinkLockError{};
            connection.rememberLinkLock(lock);
        }
        const LinkLockScope heldLock{connection, lock};
        // Serialize ORM links through the transaction boundary. A locking read observes committed winners even
        // under REPEATABLE READ. INSERT errors, including trigger errors, are always propagated.
        std::string predicate;
        for (const auto& column : spec.conflictColumns)
        {
            const auto found = std::ranges::find(spec.columns, column);
            if (found == spec.columns.end())
                throw std::invalid_argument{"Invalid insert-if-absent conflict column"};
            const auto index = static_cast<std::size_t>(found - spec.columns.begin());
            if (!predicate.empty())
                predicate += " AND ";
            predicate += detail::quoteStandardIdentifier(column, CompiledSqlFlavor::MySQLFamily) + "=" +
                         spec.valueExpressions[index];
        }
        int exists{};
        soci::statement probe =
            (session.prepare << "SELECT 1 FROM " +
                                    detail::quoteStandardIdentifier(spec.tableName, CompiledSqlFlavor::MySQLFamily) +
                                    " WHERE " + predicate + " LIMIT 1 LOCK IN SHARE MODE",
             soci::use(values), soci::into(exists));
        if (probe.execute(true) && exists == 1)
            return 0;
        soci::values insertValues;
        for (const auto& parameter : statement.parameters)
        {
            bind(insertValues, parameter.name, parameter.getBoundValue());
        }
        soci::statement prepared = (session.prepare << statement.sql, soci::use(insertValues));
        prepared.execute(true);
        return normalizeAffectedRows(prepared.get_affected_rows());
    }
    auto migrationTable(soci::session& session) const -> std::string override
    {
        std::string database;
        soci::indicator indicator{};
        session << "SELECT DATABASE()", soci::into(database, indicator);
        if (indicator == soci::i_null || database.empty())
        {
            throw migrations::MigrationError{migrations::ErrorCode::InvalidHistory,
                                             "Migrations require a selected database", 0, type_};
        }
        return detail::quoteStandardIdentifier(database, CompiledSqlFlavor::MySQLFamily) + ".`_orm_migrations`";
    }
    auto validateMigrationSession(soci::session& session) const -> void override
    {
        validateMigrationSettings(settings(session));
    }
    auto migrationJournalTable(soci::session&, std::string_view table) const -> std::string override
    {
        return std::string{table.substr(0, table.rfind('.') + 1)} + "`_orm_migrations_incomplete`";
    }
    auto migrationHistoryExists(soci::session& session, std::string_view table) const -> bool override
    {
        const auto resolved = migrationTable(session);
        if (table != resolved && table != migrationJournalTable(session, resolved))
        {
            throw migrations::MigrationError{migrations::ErrorCode::InvalidHistory,
                                             "Invalid migration history namespace", 0, type_};
        }
        const std::string name{table == resolved ? "_orm_migrations" : "_orm_migrations_incomplete"};
        soci::row object;
        soci::statement prepared =
            (session.prepare << "SELECT TABLE_TYPE,COALESCE(ENGINE,'') FROM information_schema.tables "
                                "WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME=:name",
             soci::use(name, "name"), soci::into(object));
        if (not prepared.execute(true))
        {
            return false;
        }
        if (object.get<std::string>(0) != "BASE TABLE" || object.get<std::string>(1) != "InnoDB")
        {
            throw migrations::MigrationError{migrations::ErrorCode::InvalidHistory,
                                             "Migration history and journal require InnoDB tables", 0, type_};
        }
        return true;
    }
    auto createMigrationHistory(soci::session& session, std::string_view table) const -> void override
    {
        const auto journal = migrationJournalTable(session, table);
        (void)migrationHistoryExists(session, table);
        (void)migrationHistoryExists(session, journal);
        const auto suffix = std::string{" ENGINE=InnoDB DEFAULT CHARACTER SET utf8mb4 COLLATE "} +
                            (type_ == BackendType::Mariadb ? "utf8mb4_nopad_bin" : "utf8mb4_0900_bin");
        session << "CREATE TABLE IF NOT EXISTS " + std::string{table} +
                       " (version BIGINT PRIMARY KEY NOT NULL,name LONGTEXT NOT NULL,backend INT NOT NULL,checksum "
                       "CHAR(64) NOT NULL,"
                       "applied_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,baseline INT NOT NULL CHECK(baseline "
                       "IN (0,1)))" +
                       suffix;
        session << "CREATE TABLE IF NOT EXISTS " + journal +
                       " (slot INT PRIMARY KEY NOT NULL CHECK(slot=1),version BIGINT NOT NULL,name LONGTEXT NOT "
                       "NULL,backend INT NOT NULL,"
                       "direction INT NOT NULL CHECK(direction IN (0,1)),checksum CHAR(64) NOT NULL)" +
                       suffix;
        if (!migrationHistoryExists(session, table) || !migrationHistoryExists(session, journal))
        {
            throw migrations::MigrationError{migrations::ErrorCode::InvalidHistory,
                                             "Migration history or journal was not created", 0, type_};
        }
    }
    auto beginMigration(soci::session& session, std::string_view table) const -> void override
    {
        const auto lock = lockName(table);
        int acquired{};
        soci::indicator indicator{};
        session << "SELECT GET_LOCK(:name,0)", soci::use(lock, "name"), soci::into(acquired, indicator);
        if (indicator == soci::i_null || acquired != 1)
        {
            throw migrations::MigrationError{migrations::ErrorCode::LockUnavailable, "Migration history is locked", 0,
                                             type_};
        }
    }
    auto endMigration(soci::session& session, std::string_view table) const -> void override
    {
        const auto lock = lockName(table);
        int released{};
        soci::indicator indicator{};
        session << "SELECT RELEASE_LOCK(:name)", soci::use(lock, "name"), soci::into(released, indicator);
        if (indicator == soci::i_null || released != 1)
        {
            throw migrations::MigrationError{migrations::ErrorCode::LockUnavailable,
                                             "Migration lock release could not be confirmed", 0, type_};
        }
        int stillOwned{};
        session << "SELECT IF(IS_USED_LOCK(:name)=CONNECTION_ID(),1,0)", soci::use(lock, "name"),
            soci::into(stillOwned);
        if (stillOwned == 1)
        {
            session.close();
            throw migrations::MigrationError{migrations::ErrorCode::LockUnavailable,
                                             "Migration SQL acquired a reentrant history lock", 0, type_};
        }
    }
    auto executeMigrationScript(soci::session& session, std::string_view script) const -> void override
    {
        const auto table = migrationTable(session);
        const auto lock = lockName(table);
        const auto before = settings(session);
        validateMigrationSettings(before);
        requireLock(session, lock);
        if (isEmptyScript(script))
        {
            return;
        }
        if constexpr ((std::numeric_limits<std::size_t>::max)() > (std::numeric_limits<unsigned long>::max)())
        {
            if (script.size() > (std::numeric_limits<unsigned long>::max)())
            {
                throw migrations::MigrationError{migrations::ErrorCode::InvalidSql,
                                                 "Migration SQL exceeds the client query-length limit", 0, type_};
            }
        }
        auto* connection = handle(session);
        if (orm_mysql_native::calls().setServerOption(connection, MYSQL_OPTION_MULTI_STATEMENTS_ON) != 0)
        {
            migrationFailure(orm_mysql_native::calls().errorNumber(connection));
        }
        unsigned int error = 0;
        const std::string sql{script};
        if (orm_mysql_native::calls().realQuery(connection, sql.data(), static_cast<unsigned long>(sql.size())) != 0)
        {
            error = orm_mysql_native::calls().errorNumber(connection);
        }
        else
        {
            while (true)
            {
                auto* result = orm_mysql_native::calls().storeResult(connection);
                if (result != nullptr)
                {
                    orm_mysql_native::calls().freeResult(result);
                }
                else if (orm_mysql_native::calls().fieldCount(connection) != 0)
                {
                    error = orm_mysql_native::calls().errorNumber(connection);
                    break;
                }
                const auto next = orm_mysql_native::calls().nextResult(connection);
                if (next == -1)
                    break;
                if (next > 0)
                {
                    error = orm_mysql_native::calls().errorNumber(connection);
                    break;
                }
            }
        }
        if (orm_mysql_native::calls().setServerOption(connection, MYSQL_OPTION_MULTI_STATEMENTS_OFF) != 0)
        {
            const auto disableError = orm_mysql_native::calls().errorNumber(connection);
            session.close();
            migrationFailure(disableError);
        }
        if (settings(session) != before || (connection->server_status & SERVER_STATUS_IN_TRANS) != 0)
        {
            session.close();
            throw migrations::MigrationError{migrations::ErrorCode::InvalidSql,
                                             "Migration SQL changed connection settings or transaction state", 0,
                                             type_};
        }
        requireLock(session, lock);
        if (!migrationHistoryExists(session, table) ||
            !migrationHistoryExists(session, migrationJournalTable(session, table)))
        {
            throw migrations::MigrationError{migrations::ErrorCode::InvalidHistory,
                                             "Migration SQL removed its history or journal", 0, type_};
        }
        if (error != 0)
        {
            migrationFailure(error);
        }
    }

private:
    static auto handle(soci::session& session) -> MYSQL*
    {
        return static_cast<SessionBackend*>(session.get_backend())->connectionHandle();
    }
    static auto lockName(std::string_view table) -> std::string
    {
        return "orm:" + migrations::detail::sha256(table).substr(0, 60);
    }
    auto requireLock(soci::session& session, const std::string& name) const -> void
    {
        int owned{};
        session << "SELECT IF(IS_USED_LOCK(:name)=CONNECTION_ID(),1,0)", soci::use(name, "name"), soci::into(owned);
        if (owned != 1)
        {
            session.close();
            throw migrations::MigrationError{migrations::ErrorCode::LockUnavailable, "Migration session lost its lock",
                                             0, type_};
        }
    }
    static auto settings(soci::session& session) -> std::array<std::string, 11>
    {
        std::array<std::string, 11> result;
        session << "SELECT "
                   "COALESCE(DATABASE(),''),@@SESSION.sql_mode,@@SESSION.character_set_client,@@SESSION.character_set_"
                   "connection,"
                   "COALESCE(@@SESSION.character_set_results,''),@@SESSION.collation_connection,@@SESSION.foreign_key_"
                   "checks,@@SESSION."
                   "autocommit,@@SESSION.max_sort_length,@@SESSION.time_zone,@@SESSION.explicit_defaults_for_timestamp",
            soci::into(result[0]), soci::into(result[1]), soci::into(result[2]), soci::into(result[3]),
            soci::into(result[4]), soci::into(result[5]), soci::into(result[6]), soci::into(result[7]),
            soci::into(result[8]), soci::into(result[9]), soci::into(result[10]);
        return result;
    }
    auto validateMigrationSettings(const std::array<std::string, 11>& current) const -> void
    {
        if (current[0].empty())
        {
            throw migrations::MigrationError{migrations::ErrorCode::InvalidHistory,
                                             "Migrations require a selected database", 0, type_};
        }
        const auto hasMode = [&current](std::string_view mode)
        {
            const auto modes = std::string{","} + current[1] + ",";
            return modes.find(std::string{","} + std::string{mode} + ",") != std::string::npos;
        };
        const auto collation = type_ == BackendType::Mariadb ? "utf8mb4_nopad_bin" : "utf8mb4_0900_bin";
        if (hasMode("ANSI_QUOTES") || hasMode("NO_BACKSLASH_ESCAPES") || !hasMode("STRICT_TRANS_TABLES") ||
            !hasMode("ONLY_FULL_GROUP_BY") || !hasMode("NO_ENGINE_SUBSTITUTION") ||
            !hasMode("ERROR_FOR_DIVISION_BY_ZERO") || hasMode("ORACLE") || hasMode("MAXDB") ||
            current[2] != "utf8mb4" || current[3] != "utf8mb4" || current[4] != "utf8mb4" || current[5] != collation ||
            current[6] != "1" || current[7] != "1" || (type_ == BackendType::Mariadb && current[8] != "65536") ||
            current[9] != "+00:00" || current[10] != "1")
        {
            throw migrations::MigrationError{migrations::ErrorCode::InvalidSql,
                                             "Migration SQL requires the backend's connection settings", 0, type_};
        }
    }
    [[noreturn]] auto migrationFailure(unsigned int code) const -> void
    {
        throw migrations::MigrationError{migrations::ErrorCode::Execution,
                                         "Migration SQL failed and may have partially committed", 0, type_,
                                         std::to_string(code)};
    }
    BackendType type_;
};
}

auto makeRuntime(BackendType type) -> std::unique_ptr<BackendRuntime>
{
    return std::make_unique<Runtime>(type);
}
}
