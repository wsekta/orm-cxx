#include "orm-cxx/database/sqlite/SqliteBackend.hpp"

#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "../defaults/DefaultCommandGeneratorFactory.hpp"
#include "orm-cxx/database/BackendRuntime.hpp"
#include "orm-cxx/database/binding/StatementBinding.hpp"
#include "orm-cxx/database/CommandGenerator.hpp"
#include "orm-cxx/database/DatabaseError.hpp"
#include "soci/soci.h"
#include "soci/sqlite3/soci-sqlite3.h"

namespace
{
constexpr std::string_view connectionStringPrefix{"sqlite3://"};

class SqliteRuntime final : public orm::db::BackendRuntime
{
public:
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
} // namespace orm::db::sqlite
