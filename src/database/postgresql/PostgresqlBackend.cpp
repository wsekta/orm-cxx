#include "orm-cxx/database/postgresql/PostgresqlBackend.hpp"

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
#include "soci/postgresql/soci-postgresql.h"
#include "soci/soci.h"

namespace
{
constexpr std::string_view connectionStringPrefix{"postgresql://"};

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

    if (sqlState.starts_with("25") or sqlState.starts_with("40"))
    {
        return orm::DatabaseErrorCode::Transaction;
    }

    return fallback;
}

class PostgresqlRuntime final : public orm::db::BackendRuntime
{
public:
    auto open(soci::session& session, std::string_view connectionString) const -> void override
    {
        if (connectionString.find('\0') != std::string_view::npos)
        {
            throw std::invalid_argument{"PostgreSQL connection string must not contain an embedded NUL byte"};
        }

        if (not connectionString.starts_with(connectionStringPrefix))
        {
            throw std::invalid_argument{"PostgreSQL connection string must start with postgresql://"};
        }

        session.open(*soci::factory_postgresql(), std::string{connectionString.substr(connectionStringPrefix.size())});
    }

    auto onConnect(soci::session& /*session*/) const -> void override {}

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
        return orm::db::BackendRuntimeLimits{.maxBindParameters = 65'535};
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

        const auto operationName = std::string{operation};

        return orm::DatabaseError{code, orm::db::BackendType::Postgres, operationName,
                                  "PostgreSQL operation failed: " + operationName, std::move(nativeCode)};
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

namespace orm::db::postgresql
{
PostgresqlBackend::PostgresqlBackend()
    : backendCapabilities{postgresqlCapabilities()},
      backendRuntime{std::make_unique<PostgresqlRuntime>()},
      postgresqlCommandGenerator{defaults::makeDefaultCommandGenerator(postgresqlDialect)}
{
}

PostgresqlBackend::~PostgresqlBackend() = default;

auto PostgresqlBackend::type() const noexcept -> BackendType
{
    return BackendType::Postgres;
}

auto PostgresqlBackend::acceptsConnectionString(std::string_view connectionString) const noexcept -> bool
{
    return connectionString.starts_with(connectionStringPrefix);
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
    return *postgresqlCommandGenerator;
}
} // namespace orm::db::postgresql
