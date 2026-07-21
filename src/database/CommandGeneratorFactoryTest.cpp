#include "orm-cxx/database/CommandGeneratorFactory.hpp"

#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>

#include "orm-cxx/database/sqlite/SqliteBackend.hpp"

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
    orm::db::sqlite::SqliteBackend delegate;
};
} // namespace

TEST(CommandGeneratorFactoryTest, registersSQLiteBackendAndPreservesGeneratorLookup)
{
    const orm::db::CommandGeneratorFactory factory;
    const auto& backend = factory.getBackend(orm::db::BackendType::Sqlite);

    EXPECT_EQ(backend.type(), orm::db::BackendType::Sqlite);
    EXPECT_EQ(&factory.getCommandGenerator(orm::db::BackendType::Sqlite), &backend.commandGenerator());
}

TEST(CommandGeneratorFactoryTest, findsBackendByConnectionString)
{
    const orm::db::CommandGeneratorFactory factory;

    const auto* sqlite = factory.findBackend("sqlite3://:memory:");

    ASSERT_NE(sqlite, nullptr);
    EXPECT_EQ(sqlite->type(), orm::db::BackendType::Sqlite);
#if ORM_CXX_ENABLE_POSTGRESQL_BACKEND
    const auto* postgresql = factory.findBackend("postgresql://host=localhost dbname=test");
    ASSERT_NE(postgresql, nullptr);
    EXPECT_EQ(postgresql->type(), orm::db::BackendType::Postgres);
#else
    EXPECT_EQ(factory.findBackend("postgresql://host=localhost dbname=test"), nullptr);
#endif
}

TEST(CommandGeneratorFactoryTest, rejectsDuplicateAndNullBackendRegistration)
{
    orm::db::CommandGeneratorFactory factory;

    EXPECT_THROW(factory.registerBackend(std::make_unique<orm::db::sqlite::SqliteBackend>()), std::invalid_argument);
    EXPECT_THROW(factory.registerBackend(nullptr), std::invalid_argument);
    EXPECT_THROW(factory.registerBackend(std::make_unique<DelegatingBackend>(orm::db::BackendType::Empty, false)),
                 std::invalid_argument);
}

TEST(CommandGeneratorFactoryTest, rejectsAmbiguousAutomaticBackendSelectionDeterministically)
{
    orm::db::CommandGeneratorFactory factory;
    factory.registerBackend(std::make_unique<DelegatingBackend>(orm::db::BackendType::Mysql, true));

    EXPECT_EQ(factory.findBackend("sqlite3://:memory:"), nullptr);
}

TEST(CommandGeneratorFactoryTest, preservesOutOfRangeForUnknownBackendLookup)
{
    const orm::db::CommandGeneratorFactory factory;

    EXPECT_THROW((void)factory.getBackend(orm::db::BackendType::Mysql), std::out_of_range);
    EXPECT_THROW((void)factory.getCommandGenerator(orm::db::BackendType::Mysql), std::out_of_range);
}
