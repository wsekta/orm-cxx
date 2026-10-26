module;

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <charconv>
#include <chrono>
#include <cmath>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <functional>
#include <initializer_list>
#include <libpq-fe.h>
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

#include "ConnectionParameters.hpp"
#include "soci/postgresql/soci-postgresql.h"
#include "soci/soci.h"

module orm;

import :internal;
import :connection_config;

namespace
{

auto codeFromSqlState(std::string_view sqlState, orm::DatabaseErrorCode fallback) -> orm::DatabaseErrorCode
{
    if (sqlState.starts_with("08"))
    {
        return orm::DatabaseErrorCode::Connection;
    }

    if (sqlState == "57P01" or sqlState == "57P02" or sqlState == "57P03" or sqlState == "57P04")
    {
        return orm::DatabaseErrorCode::Connection;
    }

    if (sqlState.starts_with("23"))
    {
        return orm::DatabaseErrorCode::Constraint;
    }

    if (sqlState.starts_with("22"))
    {
        return orm::DatabaseErrorCode::Conversion;
    }

    if (sqlState.starts_with("25") or sqlState.starts_with("40"))
    {
        return orm::DatabaseErrorCode::Transaction;
    }

    return fallback;
}

class PostgresqlRuntime final : public orm::db::BackendRuntime
{
public:
    auto migrationTable(soci::session& session) const -> std::string override
    {
        std::string schema;
        soci::indicator indicator{};
        session << "SELECT current_schema()", soci::into(schema, indicator);
        if (indicator == soci::i_null)
        {
            throw orm::migrations::MigrationError{orm::migrations::ErrorCode::InvalidHistory,
                                                  "PostgreSQL migration history needs a current schema", 0,
                                                  orm::db::BackendType::Postgres};
        }
        return orm::db::postgresql::PostgresqlDialect{}.quoteIdentifier(schema) + ".\"_orm_migrations\"";
    }

    auto migrationHistoryExists(soci::session& session, std::string_view historyTable) const -> bool override
    {
        const std::string table{historyTable};
        int exists{};
        session << "SELECT CASE WHEN to_regclass(:table) IS NULL THEN 0 ELSE 1 END", soci::use(table, "table"),
            soci::into(exists);
        return exists != 0;
    }

    auto createMigrationHistory(soci::session& session, std::string_view table) const -> void override
    {
        session << "CREATE TABLE IF NOT EXISTS " + std::string{table} +
                       " (version BIGINT PRIMARY KEY NOT NULL, name TEXT NOT NULL, backend INTEGER NOT NULL, "
                       "checksum TEXT NOT NULL, applied_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP, "
                       "baseline INTEGER NOT NULL CHECK(baseline IN (0,1)))";
    }

    auto beginMigration(soci::session& session, std::string_view historyTable) const -> void override
    {
        session.begin();
        try
        {
            const std::string table{historyTable};
            int locked{};
            session << "SELECT CASE WHEN pg_try_advisory_xact_lock(1869770083, hashtext(:table)) THEN 1 ELSE 0 END",
                soci::use(table, "table"), soci::into(locked);
            if (!locked)
            {
                throw orm::migrations::MigrationError{orm::migrations::ErrorCode::LockUnavailable,
                                                      "Migration history is locked", 0, orm::db::BackendType::Postgres};
            }
        }
        catch (...)
        {
            session.rollback();
            throw;
        }
    }

    auto executeMigrationScript(soci::session& session, std::string_view script) const -> void override
    {
        const auto before = migrationSettings(session);
        validateMigrationSettings(before);
        auto* handle = static_cast<soci::postgresql_session_backend*>(session.get_backend())->conn_;
        const std::string sql{script};
        auto* result = PQexec(handle, sql.c_str());
        // libpq accepts null results: fatal status, absent fields, and no-op cleanup.
        // https://www.postgresql.org/docs/current/libpq-exec.html
        const auto status = PQresultStatus(result);
        const auto* nativeCode = PQresultErrorField(result, PG_DIAG_SQLSTATE);
        const std::optional<std::string> code =
            nativeCode == nullptr ? std::nullopt : std::optional<std::string>{nativeCode};
        PQclear(result);
        if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK && status != PGRES_EMPTY_QUERY)
        {
            throw orm::migrations::MigrationError{orm::migrations::ErrorCode::Execution, "Migration SQL failed", 0,
                                                  orm::db::BackendType::Postgres, code};
        }
        if (PQtransactionStatus(handle) != PQTRANS_INTRANS)
        {
            throw orm::migrations::MigrationError{orm::migrations::ErrorCode::Execution,
                                                  "Migration transaction ended unexpectedly", 0,
                                                  orm::db::BackendType::Postgres};
        }
        if (migrationSettings(session) != before)
        {
            throw orm::migrations::MigrationError{orm::migrations::ErrorCode::InvalidSql,
                                                  "Migration SQL changed connection settings", 0,
                                                  orm::db::BackendType::Postgres};
        }
    }

    auto open(soci::session& session, orm::db::ConnectionConfigView view) const -> void override
    {
        const auto& config = view.get<orm::PostgresConfig>();
        soci::connection_parameters parameters{*soci::factory_postgresql(), ""};
        const auto invalid = []() -> void
        {
            throw orm::DatabaseError{orm::DatabaseErrorCode::Connection, orm::db::BackendType::Postgres, "connect",
                                     "Invalid PostgreSQL connection configuration"};
        };
        const auto add = [&](const char* name, const std::optional<std::string>& value)
        {
            if (value)
                parameters.set_option(name, orm_postgresql_connection_detail::prepareValue(*value));
        };
        const auto addPath = [&](const char* name, const std::optional<std::filesystem::path>& value)
        {
            if (value)
            {
                const auto utf8 = value->u8string();
                parameters.set_option(name, orm_postgresql_connection_detail::prepareValue(std::string_view{
                                                reinterpret_cast<const char*>(utf8.data()), utf8.size()}));
            }
        };
        try
        {
            add("host", config.host);
            add("dbname", config.database);
            add("user", config.user);
            add("password", config.password);
            if (config.port)
                parameters.set_option("port", std::to_string(*config.port));
            if (config.connectTimeout)
            {
                const auto seconds = config.connectTimeout->count();
                if (seconds < 0 || seconds > std::numeric_limits<int>::max())
                    invalid();
                parameters.set_option("connect_timeout", std::to_string(seconds));
            }
            addPath("passfile", config.passfile);
            if (config.tls.mode)
            {
                const char* mode = nullptr;
                switch (*config.tls.mode)
                {
                case orm::PostgresSslMode::Disable:
                    mode = "disable";
                    break;
                case orm::PostgresSslMode::Allow:
                    mode = "allow";
                    break;
                case orm::PostgresSslMode::Prefer:
                    mode = "prefer";
                    break;
                case orm::PostgresSslMode::Require:
                    mode = "require";
                    break;
                case orm::PostgresSslMode::VerifyCa:
                    mode = "verify-ca";
                    break;
                case orm::PostgresSslMode::VerifyFull:
                    mode = "verify-full";
                    break;
                default:
                    invalid();
                }
                parameters.set_option("sslmode", mode);
            }
            addPath("sslrootcert", config.tls.caCertificate);
            addPath("sslcert", config.tls.clientCertificate);
            addPath("sslkey", config.tls.clientKey);
            session.open(parameters);
        }
        catch (...)
        {
            session.close();
            throw orm::DatabaseError{orm::DatabaseErrorCode::Connection, orm::db::BackendType::Postgres, "connect",
                                     "PostgreSQL connection failed"};
        }
    }

    auto onConnect(soci::session& session) const -> void override
    {
        session << "SET DateStyle TO 'ISO, YMD'";
        session << "SET TimeZone TO 'UTC'";
    }

    auto validateMigrationSession(soci::session& session) const -> void override
    {
        validateMigrationSettings(migrationSettings(session));
    }

    [[nodiscard]] auto
    statementErrorInvalidatesTransaction(const soci::soci_error& error) const noexcept -> bool override
    {
        return dynamic_cast<const soci::postgresql_soci_error*>(&error) != nullptr;
    }

    auto tableExists(soci::session& session, std::string_view tableName) const -> bool override
    {
        auto name = std::string{tableName};
        int exists{};
        session << R"sql(
            SELECT CASE WHEN EXISTS (
                SELECT 1
                FROM pg_catalog.pg_class AS table_info
                JOIN pg_catalog.pg_namespace AS namespace_info
                  ON namespace_info.oid = table_info.relnamespace
                WHERE table_info.relname = :name
                  AND table_info.relkind IN ('r', 'p')
                  AND namespace_info.nspname = ANY (current_schemas(false))
            ) THEN 1 ELSE 0 END
        )sql",
            soci::use(name, "name"), soci::into(exists);

        return exists != 0;
    }

    auto limits(soci::session& /*session*/) const -> orm::db::BackendRuntimeLimits override
    {
        return orm::db::BackendRuntimeLimits{.maxBindParameters = 65'535, .maxIndexBytes = std::nullopt};
    }

    auto normalizeAffectedRows(long long affectedRows) const -> std::size_t override
    {
        if (affectedRows < 0)
        {
            throw std::runtime_error{"PostgreSQL did not report affected row count"};
        }

        return static_cast<std::size_t>(affectedRows);
    }

    auto bind(soci::values& values, std::string_view name, const orm::db::BoundValue& value) const -> void override
    {
        if (value.logicalType == orm::model::ColumnType::Date ||
            value.logicalType == orm::model::ColumnType::TimeOfDay ||
            value.logicalType == orm::model::ColumnType::LocalDateTime ||
            value.logicalType == orm::model::ColumnType::UtcTimestamp ||
            value.logicalType == orm::model::ColumnType::Uuid)
        {
            if (not orm::db::binding::hasCompatibleStorage(value))
                throw orm::db::binding::ConversionError{"Native value storage does not match its logical column type"};
            // PostgreSQL accepts the complete finite ORM domain; the canonical storage check validates it.
        }
        if (value.logicalType == orm::model::ColumnType::String)
        {
            if (not orm::db::binding::hasCompatibleStorage(value))
            {
                throw orm::db::binding::ConversionError{"String value storage does not match its logical column type"};
            }

            if (not value.isNull() and std::get<std::string>(value.value.value()).find('\0') != std::string::npos)
            {
                throw orm::db::binding::ConversionError{"PostgreSQL text values must not contain an embedded NUL byte"};
            }
        }

        if (value.logicalType == orm::model::ColumnType::UnsignedLongLong)
        {
            if (not orm::db::binding::hasCompatibleStorage(value))
            {
                throw orm::db::binding::ConversionError{
                    "Unsigned 64-bit value storage does not match its logical column type"};
            }

            auto postgresqlValue = orm::db::BoundValue{
                .logicalType = orm::model::ColumnType::LongLong,
                .value = std::nullopt,
            };

            if (not value.isNull())
            {
                const auto unsignedValue = std::get<unsigned long long>(value.value.value());

                if (unsignedValue > static_cast<unsigned long long>(std::numeric_limits<long long>::max()))
                {
                    throw orm::db::binding::ConversionError{
                        "PostgreSQL cannot represent an unsigned 64-bit value above INT64_MAX"};
                }

                postgresqlValue.value = orm::query::QueryValue::Value{static_cast<long long>(unsignedValue)};
            }

            orm::db::binding::bindBoundValue(values, name, postgresqlValue);
            return;
        }

        orm::db::binding::bindBoundValue(values, name, value);
    }

    auto translateError(const soci::soci_error& error, orm::DatabaseErrorCode fallback,
                        std::string_view operation) const -> orm::DatabaseError override
    {
        auto code = fallback;

        switch (error.get_error_category())
        {
        case soci::soci_error::connection_error:
            code = orm::DatabaseErrorCode::Connection;
            break;
        case soci::soci_error::constraint_violation:
            code = orm::DatabaseErrorCode::Constraint;
            break;
        case soci::soci_error::unknown_transaction_state:
            code = orm::DatabaseErrorCode::Transaction;
            break;
        case soci::soci_error::invalid_statement:
        case soci::soci_error::no_privilege:
        case soci::soci_error::no_data:
        case soci::soci_error::system_error:
        case soci::soci_error::unknown:
            break;
        }

        std::optional<std::string> nativeCode;

        if (const auto* postgresqlError = dynamic_cast<const soci::postgresql_soci_error*>(&error);
            postgresqlError != nullptr)
        {
            const auto sqlState = std::string{postgresqlError->sqlstate()};

            if (sqlState.size() == 5 and sqlState.find_first_not_of(' ') != std::string::npos)
            {
                nativeCode = sqlState;
                code = codeFromSqlState(nativeCode.value(), code);
            }
        }

        // Session initialization is part of connecting even when its SQL reports a data error.
        if (fallback == orm::DatabaseErrorCode::Connection && operation == "connect")
        {
            code = orm::DatabaseErrorCode::Connection;
        }

        const auto operationName = std::string{operation};

        return orm::DatabaseError{code, orm::db::BackendType::Postgres, operationName,
                                  "PostgreSQL operation failed: " + operationName, std::move(nativeCode)};
    }

private:
    static auto migrationSettings(soci::session& session) -> std::array<std::string, 6>
    {
        std::array<std::string, 6> values;
        session << "SELECT current_setting('search_path'),current_setting('role'),"
                   "current_setting('session_authorization'),current_setting('standard_conforming_strings'),"
                   "current_setting('DateStyle'),current_setting('TimeZone')",
            soci::into(values[0]), soci::into(values[1]), soci::into(values[2]), soci::into(values[3]),
            soci::into(values[4]), soci::into(values[5]);
        return values;
    }

    static auto validateMigrationSettings(const std::array<std::string, 6>& values) -> void
    {
        if (values[3] != "on" || values[4] != "ISO, YMD" || values[5] != "UTC")
            throw orm::migrations::MigrationError{orm::migrations::ErrorCode::InvalidSql,
                                                  "Migration SQL requires the backend's connection settings", 0,
                                                  orm::db::BackendType::Postgres};
    }
};

auto postgresqlCapabilities() -> orm::db::BackendCapabilities
{
    using orm::model::ColumnType;

    return orm::db::BackendCapabilities{
        .schema =
            {
                .createTableIfNotExists = true,
                .dropTableIfExists = true,
                .autoIncrementPrimaryKey = true,
                .compositePrimaryKeys = true,
                .foreignKeys = true,
                .onDeleteCascade = true,
                .transactionalDdl = true,
            },
        .query =
            {
                .limit = true,
                .offset = true,
                .offsetWithoutLimit = true,
                .offsetRequiresOrderBy = false,
                .projections = true,
                .groupBy = true,
                .having = true,
                .collectionPredicates = true,
                .fullModelGrouping = false,
                .distinctOrderByRequiresProjectedColumn = true,
                .strictProjectionGrouping = true,
            },
        .mutations =
            {
                .insert = true,
                .update = true,
                .remove = true,
                .atomicInsertIfAbsent = true,
                .affectedRows = orm::db::AffectedRowsSupport::Reliable,
            },
        .relations =
            {
                .toOne = true,
                .oneToMany = true,
                .manyToMany = true,
                .collectionIncludes = true,
                .collectionPredicates = true,
                .junctionTables = true,
                .compositeEndpointKeys = true,
            },
        .valueLimits =
            {
                .maxUnsignedLongLong = static_cast<unsigned long long>(std::numeric_limits<long long>::max()),
                .minDate = orm::Date{std::chrono::year{1}, std::chrono::month{1}, std::chrono::day{1}},
                .maxDate = orm::Date{std::chrono::year{9999}, std::chrono::month{12}, std::chrono::day{31}},
                .minLocalDateTime = orm::LocalDateTime{std::chrono::local_days{
                    orm::Date{std::chrono::year{1}, std::chrono::month{1}, std::chrono::day{1}}}},
                .maxLocalDateTime = orm::LocalDateTime{std::chrono::local_days{orm::Date{
                                        std::chrono::year{9999}, std::chrono::month{12}, std::chrono::day{31}}}} +
                                    std::chrono::hours{24} - std::chrono::microseconds{1},
                .minUtcTimestamp = orm::UtcTimestamp{std::chrono::sys_days{
                    orm::Date{std::chrono::year{1}, std::chrono::month{1}, std::chrono::day{1}}}},
                .maxUtcTimestamp = orm::UtcTimestamp{std::chrono::sys_days{orm::Date{
                                       std::chrono::year{9999}, std::chrono::month{12}, std::chrono::day{31}}}} +
                                   std::chrono::hours{24} - std::chrono::microseconds{1},
            },
        .migrations = {.supported = true, .atomicity = orm::db::MigrationAtomicity::WholeMigration},
        .supportedColumnTypes =
            {
                ColumnType::Bool,
                ColumnType::Char,
                ColumnType::UnsignedChar,
                ColumnType::Short,
                ColumnType::UnsignedShort,
                ColumnType::Int,
                ColumnType::UnsignedInt,
                ColumnType::LongLong,
                ColumnType::UnsignedLongLong,
                ColumnType::Float,
                ColumnType::Double,
                ColumnType::String,
                ColumnType::Date,
                ColumnType::TimeOfDay,
                ColumnType::LocalDateTime,
                ColumnType::UtcTimestamp,
                ColumnType::Uuid,
            },
        .transactions = true,
    };
}
} // namespace

namespace orm::db::postgresql
{
PostgresqlBackend::PostgresqlBackend()
    : backendCapabilities{postgresqlCapabilities()},
      backendRuntime{std::make_unique<PostgresqlRuntime>()},
      postgresqlCommandGenerator{postgresqlDialect}
{
}

PostgresqlBackend::~PostgresqlBackend() = default;

auto PostgresqlBackend::type() const noexcept -> BackendType
{
    return BackendType::Postgres;
}

auto PostgresqlBackend::capabilities() const noexcept -> const BackendCapabilities&
{
    return backendCapabilities;
}

auto PostgresqlBackend::dialect() const noexcept -> const SqlDialect&
{
    return postgresqlDialect;
}

auto PostgresqlBackend::runtime() const noexcept -> const BackendRuntime&
{
    return *backendRuntime;
}

auto PostgresqlBackend::commandGenerator() const noexcept -> const CommandGenerator&
{
    return postgresqlCommandGenerator;
}

auto PostgresqlBackend::compiledSqlFlavor() const noexcept -> CompiledSqlFlavor
{
    return CompiledSqlFlavor::PostgreSQL;
}
} // namespace orm::db::postgresql
