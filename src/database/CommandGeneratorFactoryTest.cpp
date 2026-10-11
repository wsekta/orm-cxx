module;

#include "tests/UnitTestPrelude.hpp"

module orm;

import :internal;
import :test_support;
import :foundation;
import :model;
import :expressions;
import :dynamic_query;
import :static_plan;
import :sql;
import :database;

namespace
{
class DelegatingBackend final : public orm::db::BackendProvider
{
public:
    DelegatingBackend(orm::db::BackendType typeInit, bool acceptsInit) : backendType{typeInit}, accepts{acceptsInit} {}

    auto type() const noexcept -> orm::db::BackendType override
    {
        return backendType;
    }

    auto acceptsConnectionString(std::string_view /*connectionString*/) const noexcept -> bool override
    {
        return accepts;
    }

    auto capabilities() const noexcept -> const orm::db::BackendCapabilities& override
    {
        return delegate.capabilities();
    }

    auto dialect() const noexcept -> const orm::db::SqlDialect& override
    {
        return delegate.dialect();
    }

    auto runtime() const noexcept -> const orm::db::BackendRuntime& override
    {
        return delegate.runtime();
    }

    auto commandGenerator() const noexcept -> const orm::db::CommandGenerator& override
    {
        return delegate.commandGenerator();
    }

private:
    orm::db::BackendType backendType;
    bool accepts;
    orm::tests::TestBackend delegate;
};
} // namespace

TEST(CommandGeneratorFactoryTest, registersExactlyEnabledBackendsAndPreservesGeneratorLookup)
{
    const orm::db::CommandGeneratorFactory factory;
    const std::array cases{std::pair{orm::db::BackendType::Sqlite, orm::config::sqliteBackendEnabled},
                           std::pair{orm::db::BackendType::Postgres, orm::config::postgresqlBackendEnabled},
                           std::pair{orm::db::BackendType::Mysql, orm::config::mysqlBackendEnabled},
                           std::pair{orm::db::BackendType::Mariadb, orm::config::mariadbBackendEnabled}};
    for (const auto& [type, enabled] : cases)
    {
        if (enabled)
        {
            const auto& backend = factory.getBackend(type);
            EXPECT_EQ(backend.type(), type);
            EXPECT_EQ(&factory.getCommandGenerator(type), &backend.commandGenerator());
        }
        else
            EXPECT_THROW((void)factory.getBackend(type), std::out_of_range);
    }
}

TEST(CommandGeneratorFactoryTest, findsBackendByConnectionString)
{
    const orm::db::CommandGeneratorFactory factory;

    const auto* sqlite = factory.findBackend("sqlite3://:memory:");

    if constexpr (orm::config::sqliteBackendEnabled)
    {
        ASSERT_NE(sqlite, nullptr);
        EXPECT_EQ(sqlite->type(), orm::db::BackendType::Sqlite);
    }
    else
        EXPECT_EQ(sqlite, nullptr);
    const auto* postgresql = factory.findBackend("postgresql://host=localhost dbname=test");
    if constexpr (orm::config::postgresqlBackendEnabled)
    {
        ASSERT_NE(postgresql, nullptr);
        EXPECT_EQ(postgresql->type(), orm::db::BackendType::Postgres);
    }
    else
    {
        EXPECT_EQ(postgresql, nullptr);
    }
    for (const auto& [scheme, type, enabled] :
         std::array{std::tuple{"mysql://host=localhost db=test", orm::db::BackendType::Mysql,
                               orm::config::mysqlBackendEnabled},
                    std::tuple{"mariadb://host=localhost db=test", orm::db::BackendType::Mariadb,
                               orm::config::mariadbBackendEnabled}})
    {
        const auto* backend = factory.findBackend(scheme);
        if (enabled)
        {
            ASSERT_NE(backend, nullptr);
            EXPECT_EQ(backend->type(), type);
        }
        else
            EXPECT_EQ(backend, nullptr);
    }
}

TEST(CommandGeneratorFactoryTest, rejectsDuplicateAndNullBackendRegistration)
{
    orm::db::CommandGeneratorFactory factory;

    factory.registerBackend(std::make_unique<DelegatingBackend>(orm::db::BackendType::Oracle, false));
    EXPECT_THROW(factory.registerBackend(std::make_unique<DelegatingBackend>(orm::db::BackendType::Oracle, false)),
                 std::invalid_argument);
    EXPECT_THROW(factory.registerBackend(nullptr), std::invalid_argument);
    EXPECT_THROW(factory.registerBackend(std::make_unique<DelegatingBackend>(orm::db::BackendType::Empty, false)),
                 std::invalid_argument);
}

TEST(CommandGeneratorFactoryTest, rejectsAmbiguousAutomaticBackendSelectionDeterministically)
{
    orm::db::CommandGeneratorFactory factory;
    factory.registerBackend(std::make_unique<DelegatingBackend>(orm::db::BackendType::Oracle, true));
    factory.registerBackend(std::make_unique<DelegatingBackend>(orm::db::BackendType::Firebird, true));

    EXPECT_EQ(factory.findBackend("sqlite3://:memory:"), nullptr);
}

TEST(CommandGeneratorFactoryTest, preservesOutOfRangeForUnknownBackendLookup)
{
    const orm::db::CommandGeneratorFactory factory{std::vector<std::unique_ptr<orm::db::BackendProvider>>{}};

    EXPECT_THROW((void)factory.getBackend(orm::db::BackendType::Mysql), std::out_of_range);
    EXPECT_THROW((void)factory.getCommandGenerator(orm::db::BackendType::Mysql), std::out_of_range);
}
