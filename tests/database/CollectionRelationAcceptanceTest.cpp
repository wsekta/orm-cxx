#include <algorithm>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "DatabaseTest.hpp"
#include "tests/CollectionModelsDefinitions.hpp"

namespace acceptance_models
{
struct AutoRole;

struct AutoUser
{
    int id;
    std::string name;
    orm::ManyToMany<AutoRole> roles;

    inline static constexpr orm::reflection::FixedString table_name{"collection_auto_users"};
    inline static constexpr auto auto_increment_columns = orm::autoIncrement<&AutoUser::id>();
    inline static constexpr auto relations = orm::relations(orm::manyToMany<&AutoUser::roles>()
                                                                .through<"collection_auto_user_roles">()
                                                                .ownerColumns<"user_id">()
                                                                .targetColumns<"role_id">());
};

struct AutoRole
{
    int id;
    std::string name;
    orm::ManyToMany<AutoUser> users;

    inline static constexpr orm::reflection::FixedString table_name{"collection_auto_roles"};
    inline static constexpr auto auto_increment_columns = orm::autoIncrement<&AutoRole::id>();
    inline static constexpr auto relations =
        orm::relations(orm::manyToMany<&AutoRole::users>().mappedBy<&AutoUser::roles>());
};
} // namespace acceptance_models

namespace long_key_models
{
struct Target;

struct Owner
{
    long id;
    std::string name;
    orm::ManyToMany<Target> targets;

    inline static constexpr orm::reflection::FixedString table_name{"collection_long_key_owners"};
    inline static constexpr auto relations =
        orm::relations(orm::manyToMany<&Owner::targets>().through<"collection_long_key_owner_targets">());
};

struct Target
{
    int id;
    std::string name;

    inline static constexpr orm::reflection::FixedString table_name{"collection_long_key_targets"};
};
} // namespace long_key_models

using AcceptanceSchema = orm::Schema<collection_models::User, collection_models::Role, acceptance_models::AutoUser,
                                     acceptance_models::AutoRole, long_key_models::Owner, long_key_models::Target>;
using AcceptanceDatabase = orm::Database<AcceptanceSchema>;

namespace
{
struct DatabaseSqlMember
{
    using type = soci::session orm::DatabaseCore::*;

    friend auto get(DatabaseSqlMember) -> type;
};

template <typename Tag, typename Tag::type Member>
struct PrivateMemberAccess
{
    friend auto get(Tag) -> typename Tag::type
    {
        return Member;
    }
};

template struct PrivateMemberAccess<DatabaseSqlMember, &orm::DatabaseCore::sql>;

auto sqlSession(AcceptanceDatabase& database) -> soci::session&
{
    return database.*get(DatabaseSqlMember{});
}

class QueryCountingLogger final : public soci::logger_impl
{
public:
    explicit QueryCountingLogger(std::shared_ptr<std::vector<std::string>> queries) : queries{std::move(queries)} {}

    auto start_query(const std::string& query) -> void override
    {
        queries->push_back(query);
    }

private:
    auto do_clone() const -> soci::logger_impl* override
    {
        return new QueryCountingLogger{queries};
    }

    std::shared_ptr<std::vector<std::string>> queries;
};
} // namespace

class CollectionRelationAcceptanceDatabaseTest : public DatabaseTest<AcceptanceSchema>
{
protected:
    auto createUserRoleSchema() -> void
    {
        database.createTable<collection_models::User>();
        database.createTable<collection_models::Role>();
        database.createRelationTables<collection_models::User>();
    }
};

TEST(SqliteCollectionRelationDialectTest, junctionDdlContainsOnlyEndpointKeysAndConstraints)
{
    const auto statements = orm::db::relations::createTableStatements(
        orm::modelView<collection_models::Schema, collection_models::CompositeOwner>());

    ASSERT_EQ(statements.size(), 1);
    EXPECT_EQ(statements.front(),
              "CREATE TABLE IF NOT EXISTS \"collection_owner_tags\" (\n"
              "\t\"owner_tenant\" TEXT NOT NULL,\n"
              "\t\"owner_id\" INTEGER NOT NULL,\n"
              "\t\"tag_scope\" TEXT NOT NULL,\n"
              "\t\"tag_id\" INTEGER NOT NULL,\n"
              "\tPRIMARY KEY (\"owner_tenant\", \"owner_id\", \"tag_scope\", \"tag_id\"),\n"
              "\tFOREIGN KEY (\"owner_tenant\", \"owner_id\") REFERENCES \"collection_composite_owners\" "
              "(\"tenant\", \"id\") ON "
              "DELETE CASCADE,\n"
              "\tFOREIGN KEY (\"tag_scope\", \"tag_id\") REFERENCES \"collection_composite_tags\" "
              "(\"scope\", \"id\") ON DELETE "
              "CASCADE\n"
              ");");
}

TEST_P(CollectionRelationAcceptanceDatabaseTest, manyToManyUsesReloadedAutoIncrementEndpointKeys)
{
    database.createTable<acceptance_models::AutoUser>();
    database.createTable<acceptance_models::AutoRole>();
    database.createRelationTables<acceptance_models::AutoUser>();
    database.insert(acceptance_models::AutoUser{0, "user", {}});
    database.insert(acceptance_models::AutoRole{0, "role", {}});

    orm::Query<acceptance_models::AutoUser> userQuery;
    auto users = database.select(userQuery);
    orm::Query<acceptance_models::AutoRole> roleQuery;
    const auto roles = database.select(roleQuery);

    ASSERT_EQ(users.size(), 1);
    ASSERT_EQ(roles.size(), 1);
    ASSERT_NE(users[0].id, 0);
    ASSERT_NE(roles[0].id, 0);
    EXPECT_EQ(database.link(users[0], "roles", roles[0]), 1);

    orm::Query<acceptance_models::AutoUser> includedQuery;
    includedQuery.include("roles");
    users = database.select(includedQuery);

    ASSERT_EQ(users.size(), 1);
    ASSERT_TRUE(users[0].roles.isLoaded());
    ASSERT_EQ(users[0].roles.size(), 1);
    EXPECT_EQ(users[0].roles[0].id, roles[0].id);
}

TEST_P(CollectionRelationAcceptanceDatabaseTest, includeUsesOneCollectionQueryInsteadOfOneQueryPerParent)
{
    createUserRoleSchema();
    constexpr int parentCount = 25;
    std::vector<collection_models::User> users;
    users.reserve(parentCount);
    for (int id = 1; id <= parentCount; ++id)
    {
        users.push_back({id, "user-" + std::to_string(id), {}});
    }
    const collection_models::Role sharedRole{100, "shared", {}};
    database.insert(users);
    database.insert(sharedRole);
    for (const auto& user : users)
    {
        ASSERT_EQ(database.link(user, "roles", sharedRole), 1);
    }

    const auto executedQueries = std::make_shared<std::vector<std::string>>();
    auto& session = sqlSession(database);
    const auto originalLogger = session.get_logger();
    session.set_logger(soci::logger{new QueryCountingLogger{executedQueries}});

    orm::Query<collection_models::User> query;
    query.include("roles");
    const auto returnedUsers = database.select(query);
    session.set_logger(originalLogger);

    ASSERT_EQ(returnedUsers.size(), static_cast<std::size_t>(parentCount));
    EXPECT_TRUE(std::ranges::all_of(returnedUsers,
                                    [](const auto& user) { return user.roles.isLoaded() && user.roles.size() == 1; }));
    ASSERT_EQ(executedQueries->size(), 2);
    EXPECT_TRUE(executedQueries->at(0).starts_with("SELECT "));
    EXPECT_TRUE(executedQueries->at(1).starts_with("SELECT "));
}

TEST_P(CollectionRelationAcceptanceDatabaseTest, includeGroupsLongPrimaryKeysUsingSqlRepresentation)
{
    database.createTable<long_key_models::Owner>();
    database.createTable<long_key_models::Target>();
    database.createRelationTables<long_key_models::Owner>();
    const auto ownerId = sizeof(long) > sizeof(int) ? std::numeric_limits<long>::max() : 7L;
    const long_key_models::Owner owner{ownerId, "owner", {}};
    const long_key_models::Target target{10, "target"};
    database.insert(owner);
    database.insert(target);
    ASSERT_EQ(database.link(owner, "targets", target), 1);

    orm::Query<long_key_models::Owner> query;
    query.include("targets");
    const auto owners = database.select(query);

    ASSERT_EQ(owners.size(), 1);
    EXPECT_EQ(owners[0].id, ownerId);
    ASSERT_TRUE(owners[0].targets.isLoaded());
    ASSERT_EQ(owners[0].targets.size(), 1);
    EXPECT_EQ(owners[0].targets[0].id, target.id);
}

INSTANTIATE_TEST_SUITE_P(DatabaseTest, CollectionRelationAcceptanceDatabaseTest, backendTestConfigs, backendTestName);
