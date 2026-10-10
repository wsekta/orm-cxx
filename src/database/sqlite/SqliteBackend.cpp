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

// SOCI distributions expose either full C declarations or opaque types in sqlite_api.
// clang-format off
#include "soci/sqlite3/soci-sqlite3.h"
#include <sqlite3.h>
// clang-format on

namespace sqlite_api
{
}

module orm;

import :internal;

namespace
{
using namespace sqlite_api;

template <typename Handle, typename Result, typename Native, typename... Args>
auto nativeSqliteConnection(Handle* handle, Result (*)(Native*, Args...)) -> Native*
{
    // SOCI's opaque handle and the C API's handle refer to the same SQLite connection.
    return reinterpret_cast<Native*>(handle);
}

constexpr std::string_view connectionStringPrefix{"sqlite3://"};

class SqliteRuntime final : public orm::db::BackendRuntime
{
public:
    auto migrationTable(soci::session&) const -> std::string override
    {
        return "main.\"_orm_migrations\"";
    }

    auto beginMigration(soci::session& session, std::string_view) const -> void override
    {
        try
        {
            session << "BEGIN IMMEDIATE";
        }
        catch (const soci::sqlite3_soci_error& error)
        {
            if ((error.result() & 0xff) == SQLITE_BUSY || (error.result() & 0xff) == SQLITE_LOCKED)
            {
                throw orm::migrations::MigrationError{orm::migrations::ErrorCode::LockUnavailable,
                                                      "Migration database is locked", 0, orm::db::BackendType::Sqlite,
                                                      std::to_string(error.result())};
            }
            throw;
        }
    }

    auto executeMigrationScript(soci::session& session, std::string_view script) const -> void override
    {
        auto* handle = nativeSqliteConnection(static_cast<soci::sqlite3_session_backend*>(session.get_backend())->conn_,
                                              sqlite3_get_autocommit);
        struct Authorization
        {
            bool denied = false;
            static auto check(void* context, int action, const char* first, const char* second, const char*,
                              const char*) -> int
            {
                const auto matches = [](const char* value)
                { return value != nullptr && std::string_view{value} == "_orm_migrations"; };
                if (action == SQLITE_TRANSACTION || action == SQLITE_SAVEPOINT || action == SQLITE_ATTACH ||
                    action == SQLITE_DETACH ||
                    (action == SQLITE_PRAGMA &&
                     (first == nullptr || std::string_view{first} != "defer_foreign_keys")) ||
                    matches(first) || matches(second))
                {
                    static_cast<Authorization*>(context)->denied = true;
                    return SQLITE_DENY;
                }
                return SQLITE_OK;
            }
        } authorization;
        sqlite3_set_authorizer(handle, Authorization::check, &authorization);
        struct Restore
        {
            decltype(handle) connection;
            ~Restore()
            {
                sqlite3_set_authorizer(connection, nullptr, nullptr);
            }
        } restore{handle};
        const std::string sql{script};
        const char* remaining = sql.c_str();
        const char* const end = remaining + sql.size();
        while (remaining < end)
        {
            decltype(sqlite3_next_stmt(handle, nullptr)) statement = nullptr;
            const char* tail = nullptr;
            auto code = sqlite3_prepare_v2(handle, remaining, -1, &statement, &tail);
            if (code == SQLITE_OK && statement != nullptr)
            {
                do
                {
                    code = sqlite3_step(statement);
                } while (code == SQLITE_ROW);
                const auto finalCode = sqlite3_finalize(statement);
                if (code == SQLITE_DONE)
                {
                    code = finalCode;
                }
            }
            // sqlite3_prepare_v2 guarantees a null statement on errors and empty input.
            if (code != SQLITE_OK)
            {
                throw orm::migrations::MigrationError{authorization.denied ? orm::migrations::ErrorCode::InvalidSql :
                                                                             orm::migrations::ErrorCode::Execution,
                                                      "Migration SQL failed", 0, orm::db::BackendType::Sqlite,
                                                      std::to_string(code)};
            }
            remaining = tail;
        }
        if (sqlite3_get_autocommit(handle) != 0)
        {
            throw orm::migrations::MigrationError{orm::migrations::ErrorCode::Execution,
                                                  "Migration transaction ended unexpectedly", 0,
                                                  orm::db::BackendType::Sqlite};
        }
    }

    auto open(soci::session& session, std::string_view connectionString) const -> void override
    {
        if (connectionString.find('\0') != std::string_view::npos)
        {
            throw std::invalid_argument{"SQLite connection string must not contain an embedded NUL byte"};
        }

        if (not connectionString.starts_with(connectionStringPrefix))
        {
            throw std::invalid_argument{"SQLite connection string must start with sqlite3://"};
        }

        session.open(*soci::factory_sqlite3(), std::string{connectionString.substr(connectionStringPrefix.size())});
    }

    auto onConnect(soci::session& session) const -> void override
    {
        session << "PRAGMA foreign_keys = ON;";
    }

    auto tableExists(soci::session& session, std::string_view tableName) const -> bool override
    {
        auto name = std::string{tableName};
        int count{};
        session << "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = :name;", soci::use(name, "name"),
            soci::into(count);

        return count > 0;
    }

    auto limits(soci::session& /*session*/) const -> orm::db::BackendRuntimeLimits override
    {
        return orm::db::BackendRuntimeLimits{.maxBindParameters = 900};
    }

    auto normalizeAffectedRows(long long affectedRows) const -> std::size_t override
    {
        if (affectedRows < 0)
        {
            throw std::runtime_error{"SQLite did not report affected row count"};
        }

        return static_cast<std::size_t>(affectedRows);
    }

    auto bind(soci::values& values, std::string_view name, const orm::db::BoundValue& value) const -> void override
    {
        if (value.logicalType == orm::model::ColumnType::UnsignedLongLong)
        {
            if (not orm::db::binding::hasCompatibleStorage(value))
            {
                throw orm::db::binding::ConversionError{
                    "Unsigned 64-bit value storage does not match its logical column type"};
            }

            auto sqliteValue = orm::db::BoundValue{
                .logicalType = orm::model::ColumnType::LongLong,
                .value = std::nullopt,
            };

            if (not value.isNull())
            {
                const auto unsignedValue = std::get<unsigned long long>(value.value.value());

                if (unsignedValue > static_cast<unsigned long long>(std::numeric_limits<long long>::max()))
                {
                    throw orm::db::binding::ConversionError{
                        "SQLite cannot represent an unsigned 64-bit value above INT64_MAX"};
                }

                sqliteValue.value = orm::query::QueryValue::Value{static_cast<long long>(unsignedValue)};
            }

            orm::db::binding::bindBoundValue(values, name, sqliteValue);
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

        if (const auto* sqliteError = dynamic_cast<const soci::sqlite3_soci_error*>(&error); sqliteError != nullptr)
        {
            const auto result = sqliteError->result();
            nativeCode = std::to_string(result);

            if ((result & 0xff) == SQLITE_CONSTRAINT)
            {
                code = orm::DatabaseErrorCode::Constraint;
            }
        }

        const auto operationName = std::string{operation};

        return orm::DatabaseError{code, orm::db::BackendType::Sqlite, operationName,
                                  "SQLite operation failed: " + operationName, std::move(nativeCode)};
    }
};

auto sqliteCapabilities() -> orm::db::BackendCapabilities
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
                .transactionalMigrations = true,
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
                .fullModelGrouping = true,
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
            },
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
            },
        .transactions = true,
    };
}

} // namespace

namespace orm::db::sqlite
{
SqliteBackend::SqliteBackend()
    : backendCapabilities{sqliteCapabilities()},
      backendRuntime{std::make_unique<SqliteRuntime>()},
      sqliteCommandGenerator{defaults::makeDefaultCommandGenerator(sqliteDialect)}
{
}

SqliteBackend::~SqliteBackend() = default;

auto SqliteBackend::type() const noexcept -> BackendType
{
    return BackendType::Sqlite;
}

auto SqliteBackend::acceptsConnectionString(std::string_view connectionString) const noexcept -> bool
{
    return connectionString.starts_with(connectionStringPrefix);
}

auto SqliteBackend::capabilities() const noexcept -> const BackendCapabilities&
{
    return backendCapabilities;
}

auto SqliteBackend::dialect() const noexcept -> const SqlDialect&
{
    return sqliteDialect;
}

auto SqliteBackend::runtime() const noexcept -> const BackendRuntime&
{
    return *backendRuntime;
}

auto SqliteBackend::commandGenerator() const noexcept -> const CommandGenerator&
{
    return *sqliteCommandGenerator;
}

auto SqliteBackend::compiledSqlFlavor() const noexcept -> CompiledSqlFlavor
{
    return CompiledSqlFlavor::SQLite;
}
} // namespace orm::db::sqlite
