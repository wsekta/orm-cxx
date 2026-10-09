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

class StaticPlanTest : public DatabaseTest<static_plan_models::Schema>
{
public:
    auto SetUp() -> void override
    {
        DatabaseTest::SetUp();
        if (::testing::Test::HasFatalFailure())
            return;
        createTable<Profile>();
        createTable<Role>();
        createTable<User>();
        createRelationTables<User>();
        database.insert(Profile{1, "Paris"});
        database.insert(Profile{2, "London"});
        database.insert(Role{1, "admin"});
        database.insert(Role{2, "reader"});
        database.insert(std::vector<User>{{1, 18, "Ada", std::nullopt, Profile{1, {}}, {}},
                                          {2, 21, "Grace", "grace@example.test", Profile{1, {}}, {}},
                                          {3, 17, "Linus", std::nullopt, std::nullopt, {}}});
        database.link<&User::roles>(User{1, 0, {}, {}, {}, {}}, Role{1, {}});
        database.link<&User::roles>(User{1, 0, {}, {}, {}, {}}, Role{2, {}});
        database.link<&User::roles>(User{2, 0, {}, {}, {}, {}}, Role{2, {}});
    }

    static auto ids(const std::vector<User>& users) -> std::vector<int>
    {
        auto result = std::vector<int>{};
        for (const auto& user : users)
            result.push_back(user.id);
        return result;
    }
};

TEST_P(StaticPlanTest, fullModelGroupingFollowsTheBackendCapability)
{
    constexpr auto plan =
        select<User>().groupBy(col<&User::age>()).having(countAll<User>() > 0).orderBy(asc(col<&User::age>()));
    if (database.getBackendCapabilities().query.fullModelGrouping)
    {
        const auto result = database.select(plan);
        EXPECT_EQ(ids(result), (std::vector<int>{3, 1, 2}));
    }
    else
    {
        try
        {
            (void)database.select(plan);
            FAIL() << "Expected an unsupported full-model grouping error";
        }
        catch (const orm::DatabaseError& error)
        {
            EXPECT_EQ(error.getCode(), orm::DatabaseErrorCode::UnsupportedFeature);
            EXPECT_EQ(error.getOperation(), "select");
        }
    }
}

TEST_P(StaticPlanTest, reusableSlotsMatchMutableQueriesAndRetainPagination)
{
    constexpr auto adults = select<User>()
                                .where(col<&User::age>() >= param<int, 0>())
                                .orderBy(asc(col<&User::id>()))
                                .limit(param<std::size_t, 1>())
                                .offset(param<std::size_t, 2>());
    auto dynamic = adults.toDynamic(18, std::size_t{10}, std::size_t{0});
    EXPECT_EQ(ids(database.select(adults, short{18}, std::size_t{10}, std::size_t{0})), ids(database.select(dynamic)));
    EXPECT_EQ(ids(database.select(adults, 18, std::size_t{1}, std::size_t{1})), (std::vector<int>{2}));
    EXPECT_EQ(ids(database.select(adults, 21, std::size_t{10}, std::size_t{0})), (std::vector<int>{2}));
    EXPECT_TRUE(database.select(adults, 18, std::size_t{0}, std::size_t{0}).empty());

    constexpr auto repeated =
        select<User>()
            .where((col<&User::age>() >= param<int, 0>()) && (col<&User::id>() != param<int, 0>()))
            .orderBy(asc(col<&User::id>()));
    EXPECT_EQ(ids(database.select(repeated, 18)), (std::vector<int>{1, 2}));
    EXPECT_EQ(ids(database.select(repeated, 21)), (std::vector<int>{2}));
}

TEST_P(StaticPlanTest, literalFixedListsAndContainerSlotsKeepTheirBindings)
{
    constexpr auto fixed = select<User>().where(col<&User::id>().in(std::array{1, 3})).orderBy(asc(col<&User::id>()));
    constexpr int idsArray[]{1, 3};
    constexpr auto cArray = select<User>().where(col<&User::id>().in(idsArray)).orderBy(asc(col<&User::id>()));
    constexpr auto variadic = select<User>().where(col<&User::id>().in(1, short{3})).orderBy(asc(col<&User::id>()));
    constexpr auto slots =
        select<User>().where(col<&User::id>().in(param<int, 0>(), param<int, 1>())).orderBy(asc(col<&User::id>()));
    constexpr auto arraySlot =
        select<User>().where(col<&User::id>().in(param<std::array<int, 2>, 0>())).orderBy(asc(col<&User::id>()));
    EXPECT_EQ(ids(database.select(fixed)), (std::vector<int>{1, 3}));
    EXPECT_EQ(ids(database.select(cArray)), ids(database.select(fixed)));
    EXPECT_EQ(ids(database.select(variadic)), ids(database.select(fixed)));
    EXPECT_EQ(ids(database.select(slots, 1, 3)), ids(database.select(fixed)));
    EXPECT_EQ(ids(database.select(arraySlot, std::array{1, 3})), ids(database.select(fixed)));
    EXPECT_EQ(ids(database.select(arraySlot, std::array{2, 3})), (std::vector<int>{2, 3}));

    constexpr auto notFixed = select<User>().where(col<&User::id>().notIn(1, 3));
    EXPECT_EQ(ids(database.select(notFixed)), (std::vector<int>{2}));
}

TEST_P(StaticPlanTest, nullFiltersAndRelatedPathsHaveMutableParity)
{
    constexpr auto absentEmail = select<User>().where(col<&User::email>().isNull()).orderBy(asc(col<&User::id>()));
    EXPECT_EQ(ids(database.select(absentEmail)), (std::vector<int>{1, 3}));
    constexpr auto presentEmail = select<User>().where(col<&User::email>() != std::nullopt);
    EXPECT_EQ(ids(database.select(presentEmail)), (std::vector<int>{2}));
    constexpr auto related = select<User>()
                                 .where(col<&User::profile, &Profile::city>() == param<std::string, 0>())
                                 .orderBy(asc(col<&User::id>()));
    auto dynamic = related.toDynamic(std::string{"Paris"});
    EXPECT_EQ(ids(database.select(related, "Paris")), ids(database.select(dynamic)));
    EXPECT_TRUE(database.select(related, "London").empty());
    constexpr auto absentProfile = select<User>().where(col<&User::profile, &Profile::city>().isNull());
    EXPECT_EQ(ids(database.select(absentProfile)), (std::vector<int>{3}));
    constexpr auto keyOnly =
        select<User>().where(col<&User::profile, &Profile::id>() == 1).disableJoining().orderBy(asc(col<&User::id>()));
    const auto rows = database.select(keyOnly);
    ASSERT_EQ(rows.size(), 2);
    ASSERT_TRUE(rows.front().profile.has_value());
    EXPECT_EQ(rows.front().profile->id, 1);
    EXPECT_TRUE(rows.front().profile->city.empty());
}

TEST_P(StaticPlanTest, collectionPredicatesAndIncludesRetainParentPagination)
{
    constexpr auto admins = select<User>()
                                .where(any<&User::roles>(col<&Role::name>() == param<std::string, 0>()))
                                .include<&User::roles>()
                                .include<&User::roles>()
                                .orderBy(asc(col<&User::id>()))
                                .limit(1);
    const auto rows = database.select(admins, "admin");
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows.front().id, 1);
    EXPECT_TRUE(rows.front().roles.isLoaded());
    EXPECT_EQ(rows.front().roles.size(), 2);
    auto dynamic = admins.toDynamic(std::string{"admin"});
    EXPECT_EQ(ids(rows), ids(database.select(dynamic)));

    constexpr auto noRoles = select<User>().where(!exists<&User::roles>()).include<&User::roles>();
    const auto empty = database.select(noRoles);
    ASSERT_EQ(empty.size(), 1);
    EXPECT_EQ(empty.front().id, 3);
    EXPECT_TRUE(empty.front().roles.isLoaded());
    EXPECT_TRUE(empty.front().roles.empty());

    constexpr auto noAdmin =
        select<User>().where(none<&User::roles>(col<&Role::id>() == param<int, 0>())).orderBy(asc(col<&User::id>()));
    EXPECT_EQ(ids(database.select(noAdmin, 1)), (std::vector<int>{2, 3}));
}

TEST_P(StaticPlanTest, projectionAliasesAndAggregateHavingHaveMutableParity)
{
    constexpr auto names = selectAs<User, UserName>(as<"name">(col<&User::name>()))
                               .where(col<&User::age>() >= param<int, 0>())
                               .orderBy(asc(col<&User::id>()));
    const auto projected = database.select(names, 18);
    ASSERT_EQ(projected.size(), 2);
    EXPECT_EQ(projected[0].name, "Ada");
    EXPECT_EQ(projected[1].name, "Grace");

    constexpr auto stats =
        selectAs<User, UserStats>(as<"city">(col<&User::profile, &Profile::city>()), as<"users">(countAll<User>()),
                                  as<"totalAge">(sum(col<&User::age>())), as<"averageAge">(avg(col<&User::age>())),
                                  as<"minimumAge">(min(col<&User::age>())), as<"maximumAge">(max(col<&User::age>())))
            .groupBy(col<&User::profile, &Profile::city>())
            .having(countAll<User>() >= param<long long, 0>());
    auto dynamic = stats.toDynamic(2);
    const auto groups = database.select(stats, 2);
    const auto dynamicGroups = database.select(dynamic);
    ASSERT_EQ(groups.size(), 1);
    ASSERT_EQ(dynamicGroups.size(), 1);
    EXPECT_EQ(groups[0].city, dynamicGroups[0].city);
    EXPECT_EQ(groups[0].city, "Paris");
    EXPECT_EQ(groups[0].users, 2);
    EXPECT_EQ(groups[0].totalAge, 39);
    EXPECT_EQ(groups[0].averageAge, 19.5);
    EXPECT_EQ(groups[0].minimumAge, 18);
    EXPECT_EQ(groups[0].maximumAge, 21);
    EXPECT_EQ(database.select(stats, 1).size(), 2);
}

TEST_P(StaticPlanTest, updatesBindOptionalValuesAndRelatedPrimaryKeys)
{
    constexpr auto patch = update<User>()
                               .set(col<&User::age>(), param<int, 0>())
                               .set(col<&User::email>(), param<std::optional<std::string>, 1>())
                               .where(col<&User::id>() == param<int, 2>());
    EXPECT_EQ(database.update(patch, short{30}, std::optional<std::string>{"ada@example.test"}, 1), 1);
    EXPECT_EQ(database.update(patch, 31, std::optional<std::string>{}, 1), 1);
    constexpr auto byId = select<User>().where(col<&User::id>() == param<int, 0>());
    const auto rows = database.select(byId, 1);
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows.front().age, 31);
    EXPECT_FALSE(rows.front().email.has_value());

    constexpr auto moveProfile = update<User>()
                                     .set(col<&User::profile, &Profile::id>(), param<int, 0>())
                                     .where(col<&User::id>() == param<int, 1>());
    EXPECT_EQ(database.update(moveProfile, 2, 1), 1);
    const auto moved = database.select(byId, 1);
    ASSERT_EQ(moved.size(), 1);
    ASSERT_TRUE(moved.front().profile.has_value());
    EXPECT_EQ(moved.front().profile->city, "London");

    auto dynamicPatch = patch.toDynamic(32, std::optional<std::string>{"grace@example.test"}, 2);
    EXPECT_EQ(database.update(dynamicPatch), 1);
    EXPECT_EQ(database.select(byId, 2).front().age, 32);
}

TEST_P(StaticPlanTest, deletePlansKeepBindingsAndCollectionWritePredicates)
{
    constexpr auto eraseById = remove<User>().where(col<&User::id>() == param<int, 0>());
    EXPECT_EQ(database.remove(eraseById, 3), 1);
    EXPECT_EQ(database.remove(eraseById, 3), 0);
    EXPECT_EQ(database.remove<User>(eraseById.toDynamic(2)), 1);
    constexpr auto eraseAdmins = remove<User>().where(any<&User::roles>(col<&Role::name>() == param<std::string, 0>()));
    EXPECT_EQ(database.remove(eraseAdmins, "admin"), 1);
    EXPECT_TRUE(database.select(select<User>()).empty());
    EXPECT_EQ(database.select(select<Role>()).size(), 2);
}

TEST_P(StaticPlanTest, runtimeCardinalityRawSqlAndAliasesKeepFallbackParity)
{
    const auto runtime = select<User>().where(col<&User::id>().in(std::vector{1, 3})).orderBy(asc(col<&User::id>()));
    EXPECT_EQ(ids(database.select(runtime)), (std::vector<int>{1, 3}));
    const auto initializer = select<User>().where(col<&User::id>().in({1, 3})).orderBy(asc(col<&User::id>()));
    EXPECT_EQ(ids(database.select(initializer)), ids(database.select(runtime)));
    constexpr auto listSlot =
        select<User>().where(col<&User::id>().in(param<std::vector<int>, 0>())).orderBy(asc(col<&User::id>()));
    EXPECT_EQ(ids(database.select(listSlot, std::vector{1, 3})), ids(database.select(runtime)));
    EXPECT_THROW((void)database.select(listSlot, std::vector<int>{}), std::invalid_argument);
    const auto rawPlan = select<User>()
                             .where(raw<User>("static_plan_users.id >= :id", param("id", 2)))
                             .orderBy(rawOrder<User>("static_plan_users.id DESC"));
    EXPECT_EQ(ids(database.select(rawPlan)), (std::vector<int>{3, 2}));
    const auto alias =
        selectAs<User, UserName>(as(std::string{"name"}, col<&User::name>())).where(col<&User::id>() == 1);
    EXPECT_EQ(database.select(alias).front().name, "Ada");
}

TEST_P(StaticPlanTest, boundStringsRemainParametersAndOwnedValuesSurviveMutation)
{
    constexpr auto byName = select<User>().where(col<&User::name>() == param<std::string, 0>());
    EXPECT_TRUE(database.select(byName, "Ada' OR 1 = 1 --").empty());
    auto name = std::string{"Ada"};
    const auto owned = select<User>().where(col<&User::name>() == name);
    name = "Grace";
    const auto rows = database.select(owned);
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows.front().id, 1);
}

INSTANTIATE_TEST_SUITE_P(DatabaseTest, StaticPlanTest, conformanceBackendTestConfigs, backendTestName);

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
        return orm::db::BackendType::Mysql;
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
        database = std::make_unique<static_plan_models::Database>(std::move(factory));
        database->connect(orm::db::BackendType::Mysql, "sqlite3://:memory:");
        database->createTable<Profile>();
        database->createTable<User>();
        database->insert(User{1, 18, "Ada", {}, {}, {}});
    }

    ObservedBackend* backend{};
    std::unique_ptr<static_plan_models::Database> database;
};
} // namespace

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
