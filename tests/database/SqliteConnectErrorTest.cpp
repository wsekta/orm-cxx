module;

#include "tests/UnitTestPrelude.hpp"

module orm;

import :internal;
import :connection_config;
import :test_support;
import :foundation;
import :model;
import :expressions;
import :dynamic_query;
import :static_plan;
import :sql;
import :database;

using namespace orm::test::fixtures;

namespace
{
using EmptyDatabase = orm::Database;

struct ConnectFaultState
{
    bool failInitialization{};
};

class ThrowingConnectRuntime final : public orm::db::BackendRuntime
{
public:
    ThrowingConnectRuntime(const orm::db::BackendRuntime& delegateInit, const ConnectFaultState& faultStateInit)
        : delegate{delegateInit}, faultState{faultStateInit}
    {
    }

    auto open(soci::session& session, orm::db::ConnectionConfigView config) const -> void override
    {
        delegate.open(session, config);
    }

    auto onConnect(soci::session& session) const -> void override
    {
        if (faultState.failInitialization)
            throw std::runtime_error{"test runtime initialization failure"};
        delegate.onConnect(session);
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
    const ConnectFaultState& faultState;
};

class ThrowingConnectBackend final : public orm::db::BackendProvider
{
public:
    explicit ThrowingConnectBackend(const ConnectFaultState& faultState) : runtimeStrategy{sqlite.runtime(), faultState}
    {
    }

    auto type() const noexcept -> orm::db::BackendType override
    {
        return orm::db::BackendType::Sqlite;
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

TEST(DatabaseErrorTest, nonDriverConnectHookFailureClosesSessionAndAllowsReconnect)
{
    ConnectFaultState faultState;
    std::vector<std::unique_ptr<orm::db::BackendProvider>> providers;
    providers.push_back(std::make_unique<ThrowingConnectBackend>(faultState));
    auto database =
        EmptyDatabase{orm::SqliteConfig{orm::SqliteMemory{}}, orm::db::CommandGeneratorFactory{std::move(providers)}};
    database.disconnect();
    faultState.failInitialization = true;

    try
    {
        database.connect(orm::SqliteConfig{orm::SqliteMemory{}});
        FAIL() << "Expected backend initialization failure";
    }
    catch (const orm::DatabaseError& error)
    {
        EXPECT_EQ(error.getCode(), orm::DatabaseErrorCode::Connection);
        EXPECT_EQ(error.getBackendType(), orm::db::BackendType::Sqlite);
        EXPECT_EQ(error.getOperation(), "connect");
    }

    EXPECT_FALSE(database.isConnected());
    faultState.failInitialization = false;
    EXPECT_NO_THROW(database.connect(orm::SqliteConfig{orm::SqliteMemory{}}));
    EXPECT_TRUE(database.isConnected());
    database.disconnect();
}
