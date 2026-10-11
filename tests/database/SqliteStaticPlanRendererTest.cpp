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

using namespace orm::test::fixtures;

using namespace orm::query;
using namespace static_plan_models;

namespace
{
struct RendererCalls
{
    int selects{};
    int updates{};
    int removes{};
};

class ObservedCommands final : public orm::db::commands::CreateTableCommand,
                               public orm::db::commands::DropTableCommand,
                               public orm::db::commands::InsertCommand,
                               public orm::db::commands::SelectCommand,
                               public orm::db::commands::UpdateCommand,
                               public orm::db::commands::DeleteCommand
{
public:
    ObservedCommands(const orm::db::CommandGenerator& delegate, RendererCalls& calls) : delegate{delegate}, calls{calls}
    {
    }

    auto createTable(orm::model::ModelView model) const -> std::string override
    {
        return delegate.createTable(model);
    }
    auto dropTable(orm::model::ModelView model) const -> std::string override
    {
        return delegate.dropTable(model);
    }
    auto insert(orm::model::ModelView model) const -> std::string override
    {
        return delegate.insert(model);
    }
    auto select(orm::model::ModelView model,
                const orm::query::detail::SelectSpec& spec) const -> orm::db::SelectStatement override
    {
        ++calls.selects;
        return delegate.select(model, spec);
    }
    auto update(orm::model::ModelView model,
                const orm::query::detail::UpdateSpec& spec) const -> orm::db::Statement override
    {
        ++calls.updates;
        return delegate.update(model, spec);
    }
    auto remove(orm::model::ModelView model,
                const orm::query::detail::Predicate& predicate) const -> orm::db::Statement override
    {
        ++calls.removes;
        return delegate.remove(model, predicate);
    }

private:
    const orm::db::CommandGenerator& delegate;
    RendererCalls& calls;
};

class ObservedBackend : public orm::db::BackendProvider
{
public:
    ObservedBackend()
        : generator{std::make_unique<ObservedCommands>(sqlite.commandGenerator(), calls),
                    std::make_unique<ObservedCommands>(sqlite.commandGenerator(), calls),
                    std::make_unique<ObservedCommands>(sqlite.commandGenerator(), calls),
                    std::make_unique<ObservedCommands>(sqlite.commandGenerator(), calls),
                    std::make_unique<ObservedCommands>(sqlite.commandGenerator(), calls),
                    std::make_unique<ObservedCommands>(sqlite.commandGenerator(), calls)}
    {
    }
    auto type() const noexcept -> orm::db::BackendType override
    {
        return orm::db::BackendType::Oracle;
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
        return sqlite.runtime();
    }
    auto commandGenerator() const noexcept -> const orm::db::CommandGenerator& override
    {
        return generator;
    }

    RendererCalls calls;

private:
    orm::db::sqlite::SqliteBackend sqlite;
    orm::db::CommandGenerator generator;
};

class CachedObservedBackend final : public ObservedBackend
{
public:
    auto compiledSqlFlavor() const noexcept -> orm::db::CompiledSqlFlavor override
    {
        return orm::db::CompiledSqlFlavor::SQLite;
    }
};

struct ObserverBundle
{
    template <typename Backend>
    explicit ObserverBundle(std::unique_ptr<Backend> provider)
    {
        backend = provider.get();
        orm::db::CommandGeneratorFactory factory;
        factory.registerBackend(std::move(provider));
        connection = std::make_unique<orm::Database>(std::move(factory));
        database = std::make_unique<static_plan_models::Context>(connection->orm<static_plan_models::Schema>());
        connection->connect(orm::db::BackendType::Oracle, "sqlite3://:memory:");
        database->createTable<Profile>();
        database->createTable<User>();
        database->insert(User{1, 18, "Ada", {}, {}, {}});
    }

    ObservedBackend* backend{};
    std::unique_ptr<orm::Database> connection;
    std::unique_ptr<static_plan_models::Context> database;
};
} // namespace

TEST(StaticPlanRendererTest, existingContextUsesNewBackendAfterReconnect)
{
    ObserverBundle bundle{std::make_unique<CachedObservedBackend>()};
    auto context = *bundle.database;
    constexpr auto byId = select<User>().where(col<&User::id>() == param<int, 0>());
    EXPECT_EQ(context.select(byId, 1).size(), 1);
    bundle.connection->disconnect();
    bundle.connection->connect(orm::db::BackendType::Sqlite, "sqlite3://:memory:");
    context.createTable<Profile>();
    context.createTable<User>();
    context.insert(User{2, 21, "Grace", {}, {}, {}});
    EXPECT_EQ(bundle.connection->getBackendType(), orm::db::BackendType::Sqlite);
    EXPECT_TRUE(context.select(byId, 1).empty());
    EXPECT_EQ(context.select(byId, 2).size(), 1);
}

TEST(StaticPlanRendererTest, supportedStaticPlansBypassTheRuntimeCommandGenerator)
{
    ObserverBundle bundle{std::make_unique<CachedObservedBackend>()};
    constexpr auto byId = select<User>().where(col<&User::id>() == param<int, 0>());
    constexpr auto patch =
        update<User>().set(col<&User::age>(), param<int, 0>()).where(col<&User::id>() == param<int, 1>());
    constexpr auto erase = remove<User>().where(col<&User::id>() == param<int, 0>());
    EXPECT_EQ(bundle.database->select(byId, 1).size(), 1);
    EXPECT_TRUE(bundle.database->select(byId, 2).empty());
    EXPECT_EQ(bundle.database->update(patch, 21, 1), 1);
    EXPECT_EQ(bundle.database->remove(erase, 1), 1);
    EXPECT_EQ(bundle.backend->calls.selects, 0);
    EXPECT_EQ(bundle.backend->calls.updates, 0);
    EXPECT_EQ(bundle.backend->calls.removes, 0);
}

TEST(StaticPlanRendererTest, unknownProviderAndRuntimeShapesRetainTheRuntimeRenderer)
{
    ObserverBundle custom{std::make_unique<ObservedBackend>()};
    constexpr auto byId = select<User>().where(col<&User::id>() == param<int, 0>());
    EXPECT_EQ(custom.database->select(byId, 1).size(), 1);
    EXPECT_EQ(custom.backend->calls.selects, 1);
    constexpr auto patch =
        update<User>().set(col<&User::age>(), param<int, 0>()).where(col<&User::id>() == param<int, 1>());
    constexpr auto erase = remove<User>().where(col<&User::id>() == param<int, 0>());
    EXPECT_EQ(custom.database->update(patch, 21, 1), 1);
    EXPECT_EQ(custom.backend->calls.updates, 1);
    EXPECT_EQ(custom.database->remove(erase, 1), 1);
    EXPECT_EQ(custom.backend->calls.removes, 1);

    ObserverBundle cached{std::make_unique<CachedObservedBackend>()};
    const auto runtime = select<User>().where(col<&User::id>().in(std::vector{1}));
    EXPECT_EQ(cached.database->select(runtime).size(), 1);
    EXPECT_EQ(cached.backend->calls.selects, 1);
    auto dynamic = byId.toDynamic(1);
    EXPECT_EQ(cached.database->select(dynamic).size(), 1);
    EXPECT_EQ(cached.backend->calls.selects, 2);
    constexpr auto invalidJoin =
        select<User>().where(col<&User::profile, &Profile::city>() == "Paris").disableJoining();
    EXPECT_THROW((void)cached.database->select(invalidJoin), std::invalid_argument);
}
