#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#include "orm-cxx/database.hpp"
#include "orm-cxx/database/sqlite/SqliteBackend.hpp"

namespace
{
class ThrowingConnectRuntime final : public orm::db::BackendRuntime
{
public:
    explicit ThrowingConnectRuntime(const orm::db::BackendRuntime& delegateInit) : delegate{delegateInit} {}

    auto onConnect(soci::session& /*session*/) const -> void override
    {
        throw std::runtime_error{"test runtime initialization failure"};
    }

    auto tableExists(soci::session& session, std::string_view tableName) const -> bool override
    {
        return delegate.tableExists(session, tableName);
    }

    auto limits(soci::session& session) const -> orm::db::BackendRuntimeLimits override
    {
        return delegate.limits(session);
    }

    auto normalizeAffectedRows(long long affectedRows) const -> std::size_t override
    {
        return delegate.normalizeAffectedRows(affectedRows);
    }

    auto bind(soci::values& values, std::string_view name, const orm::db::BoundValue& value) const -> void override
    {
        delegate.bind(values, name, value);
    }

    auto translateError(const soci::soci_error& error, orm::DatabaseErrorCode fallback,
                        std::string_view operation) const -> orm::DatabaseError override
    {
        return delegate.translateError(error, fallback, operation);
    }

private:
    const orm::db::BackendRuntime& delegate;
};

class ThrowingConnectBackend final : public orm::db::BackendProvider
{
public:
    ThrowingConnectBackend() : runtimeStrategy{sqlite.runtime()} {}

    auto type() const noexcept -> orm::db::BackendType override
    {
        return orm::db::BackendType::Postgres;
    }

    auto acceptsConnectionString(std::string_view connectionString) const noexcept -> bool override
    {
        return connectionString.starts_with("sqlite3://");
    }

    auto capabilities() const noexcept -> const orm::db::BackendCapabilities& override
    {
        return sqlite.capabilities();
    }

    auto dialect() const noexcept -> const orm::db::SqlDialect& override
    {
        return sqlite.dialect();
    }

    auto runtime() const noexcept -> const orm::db::BackendRuntime& override
    {
        return runtimeStrategy;
    }

    auto commandGenerator() const noexcept -> const orm::db::CommandGenerator& override
    {
        return sqlite.commandGenerator();
    }

private:
    orm::db::sqlite::SqliteBackend sqlite;
    ThrowingConnectRuntime runtimeStrategy;
};
} // namespace

static_assert(std::is_base_of_v<std::runtime_error, orm::DatabaseError>);

TEST(DatabaseErrorTest, shouldExposeStructuredBackendContext)
{
    const auto error = orm::DatabaseError{
        orm::DatabaseErrorCode::Constraint,
        orm::db::BackendType::Sqlite,
        "insert",
        "Database constraint violation",
        std::string{"19"},
    };

    EXPECT_EQ(error.getCode(), orm::DatabaseErrorCode::Constraint);
    EXPECT_EQ(error.getBackendType(), orm::db::BackendType::Sqlite);
    EXPECT_EQ(error.getOperation(), "insert");
    EXPECT_EQ(error.getNativeCode(), std::optional<std::string>{"19"});
    EXPECT_STREQ(error.what(), "Database constraint violation");
}

TEST(DatabaseErrorTest, nativeCodeShouldBeOptional)
{
    const auto error = orm::DatabaseError{
        orm::DatabaseErrorCode::NotConnected,
        orm::db::BackendType::Empty,
        "select",
        "Database is not connected",
    };

    EXPECT_FALSE(error.getNativeCode().has_value());
}

TEST(DatabaseErrorTest, shouldRemainCatchableAsRuntimeError)
{
    EXPECT_THROW((throw orm::DatabaseError{orm::DatabaseErrorCode::Transaction, orm::db::BackendType::Sqlite, "commit",
                                           "Transaction commit failed"}),
                 std::runtime_error);
}

TEST(DatabaseErrorTest, diagnosticShouldContainOnlyTheSanitizedMessage)
{
    const auto connectionString = std::string{"sqlite3://secret-database"};
    const auto parameterValue = std::string{"secret-parameter"};
    const auto error = orm::DatabaseError{
        orm::DatabaseErrorCode::Connection,
        orm::db::BackendType::Sqlite,
        "connect",
        "Database connection failed",
    };
    const auto diagnostic = std::string{error.what()};

    EXPECT_EQ(diagnostic, "Database connection failed");
    EXPECT_EQ(diagnostic.find(connectionString), std::string::npos);
    EXPECT_EQ(diagnostic.find(parameterValue), std::string::npos);
}

TEST(DatabaseErrorTest, nonDriverConnectHookFailureClosesSessionAndAllowsReconnect)
{
    auto factory = orm::db::CommandGeneratorFactory{};
    factory.registerBackend(std::make_unique<ThrowingConnectBackend>());
    auto database = orm::Database{std::move(factory)};

    try
    {
        database.connect(orm::db::BackendType::Postgres, "sqlite3://:memory:");
        FAIL() << "Expected backend initialization failure";
    }
    catch (const orm::DatabaseError& error)
    {
        EXPECT_EQ(error.getCode(), orm::DatabaseErrorCode::Connection);
        EXPECT_EQ(error.getBackendType(), orm::db::BackendType::Postgres);
        EXPECT_EQ(error.getOperation(), "connect");
    }

    EXPECT_FALSE(database.isConnected());
    EXPECT_NO_THROW(database.connect(orm::db::BackendType::Sqlite, "sqlite3://:memory:"));
    EXPECT_TRUE(database.isConnected());
    database.disconnect();
}
