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

struct RendererConfig
{
    orm::SqliteConfig sqlite;
};
template <>
struct orm::db::ConnectionTraits<RendererConfig>
{
    static constexpr auto backendType = orm::db::BackendType::Oracle;
};

namespace
{
struct RendererCalls
{
    int selects{};
    int updates{};
    int removes{};
};

class ObservedCommands final : public orm::db::CommandGenerator
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

class RendererRuntime final : public orm::db::BackendRuntime
{
public:
    explicit RendererRuntime(const orm::db::BackendRuntime& delegate) : delegate{delegate} {}
    auto open(soci::session& session, orm::db::ConnectionConfigView view) const -> void override
    {
        const auto& config = view.get<RendererConfig>();
        delegate.open(session, orm::db::ConnectionConfigView{config.sqlite});
    }
    auto onConnect(soci::session& s) const -> void override
    {
        delegate.onConnect(s);
    }
    auto tableExists(soci::session& s, std::string_view n) const -> bool override
    {
        return delegate.tableExists(s, n);
    }
    auto limits(soci::session& s) const -> orm::db::BackendRuntimeLimits override
    {
        return delegate.limits(s);
    }
    auto normalizeAffectedRows(long long n) const -> std::size_t override
    {
        return delegate.normalizeAffectedRows(n);
    }
    auto bind(soci::values& v, std::string_view n, const orm::db::BoundValue& b) const -> void override
    {
        delegate.bind(v, n, b);
    }
    auto translateError(const soci::soci_error& e, orm::DatabaseErrorCode c,
                        std::string_view op) const -> orm::DatabaseError override
    {
        return delegate.translateError(e, c, op);
    }

private:
    const orm::db::BackendRuntime& delegate;
};

class ObservedBackend : public orm::db::BackendProvider
{
public:
    ObservedBackend() : generator{sqlite.commandGenerator(), calls}, runtime_{sqlite.runtime()} {}
    auto type() const noexcept -> orm::db::BackendType override
    {
        return orm::db::BackendType::Oracle;
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
        return runtime_;
    }
    auto commandGenerator() const noexcept -> const orm::db::CommandGenerator& override
    {
        return generator;
    }

    RendererCalls calls;

private:
    orm::db::sqlite::SqliteBackend sqlite;
    ObservedCommands generator;
    RendererRuntime runtime_;
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
        connection = std::make_unique<orm::Database>(RendererConfig{orm::SqliteConfig{.database = orm::SqliteMemory{}}},
                                                     std::move(factory));
        database = std::make_unique<static_plan_models::Context>(connection->orm<static_plan_models::Schema>());
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
    bundle.connection->connect(orm::SqliteConfig{.database = orm::SqliteMemory{}});
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
