#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

#include "DatabaseTest.hpp"
#include "tests/CollectionModelsDefinitions.hpp"

namespace
{
template <typename Collection>
auto sortedIds(const Collection& collection) -> std::vector<int>
{
    std::vector<int> ids;
    ids.reserve(collection.size());
    for (const auto& value : collection)
    {
        ids.push_back(value.id);
    }
    std::ranges::sort(ids);
    return ids;
}
} // namespace

class CollectionRelationDatabaseTest : public DatabaseTest
{
protected:
    auto createAuthorBookSchema() -> void
    {
        database.createTable<collection_models::Author>();
        database.createTable<collection_models::Book>();
    }

    auto createUserRoleSchema() -> void
    {
        database.createTable<collection_models::User>();
        database.createTable<collection_models::Role>();
        database.createRelationTables<collection_models::User>();
    }
};

TEST_P(CollectionRelationDatabaseTest, oneToManyLinkAndUnlink_shouldReparentNullableChildIdempotently)
{
    createAuthorBookSchema();
    const collection_models::Author firstAuthor{1, "first", {}};
    const collection_models::Author secondAuthor{2, "second", {}};
    const collection_models::Book book{10, "book", std::nullopt};
    database.insert(std::vector{firstAuthor, secondAuthor});
    database.insert(book);

    EXPECT_EQ(database.link(firstAuthor, "books", book), 1);
    EXPECT_EQ(database.link(firstAuthor, "books", book), 0);
    EXPECT_EQ(database.link(secondAuthor, "books", book), 1);

    orm::Query<collection_models::Author> included;
    included.include("books").orderBy(orm::query::asc(orm::query::col("id")));
    auto authors = database.select(included);

    ASSERT_EQ(authors.size(), 2);
    EXPECT_TRUE(authors[0].books.isLoaded());
    EXPECT_TRUE(authors[0].books.empty());
    EXPECT_TRUE(authors[1].books.isLoaded());
    ASSERT_EQ(authors[1].books.size(), 1);
    EXPECT_EQ(authors[1].books[0].id, book.id);

    EXPECT_EQ(database.unlink(secondAuthor, "books", book), 1);
    EXPECT_EQ(database.unlink(secondAuthor, "books", book), 0);

    orm::Query<collection_models::Author> afterUnlink;
    afterUnlink.where(orm::query::col("id") == secondAuthor.id).include("books");
    authors = database.select(afterUnlink);

    ASSERT_EQ(authors.size(), 1);
    EXPECT_TRUE(authors[0].books.isLoaded());
    EXPECT_TRUE(authors[0].books.empty());
}

TEST_P(CollectionRelationDatabaseTest, oneToManyUnlink_shouldRejectNonNullableMappedBy)
{
    database.createTable<collection_models::RequiredAuthor>();
    database.createTable<collection_models::RequiredBook>();
    const collection_models::RequiredAuthor author{1, "author", {}};
    const collection_models::RequiredBook book{10, "book", author};
    database.insert(author);
    database.insert(book);

    EXPECT_EQ(database.link(author, "books", book), 0);
    EXPECT_THROW((void)database.unlink(author, "books", book), std::invalid_argument);
}

TEST_P(CollectionRelationDatabaseTest, oneToManyForeignKey_shouldBlockDeletingParentWithChildren)
{
    createAuthorBookSchema();
    const collection_models::Author author{1, "author", {}};
    const collection_models::Book book{10, "book", author};
    database.insert(author);
    database.insert(book);

    EXPECT_ANY_THROW((void)database.remove<collection_models::Author>(orm::query::col("id") == author.id));
}

TEST_P(CollectionRelationDatabaseTest, includeWithJoiningDisabled_shouldStillLoadCollectionButNotNestedObjects)
{
    createAuthorBookSchema();
    const collection_models::Author author{1, "author", {}};
    const collection_models::Book book{10, "book", author};
    database.insert(author);
    database.insert(book);

    orm::Query<collection_models::Author> query;
    query.disableJoining().include("books");
    const auto authors = database.select(query);

    ASSERT_EQ(authors.size(), 1);
    ASSERT_TRUE(authors[0].books.isLoaded());
    ASSERT_EQ(authors[0].books.size(), 1);
    ASSERT_TRUE(authors[0].books[0].author.has_value());
    EXPECT_EQ(authors[0].books[0].author->id, author.id);
    EXPECT_FALSE(authors[0].books[0].author->books.isLoaded());
}

TEST_P(CollectionRelationDatabaseTest, createRelationTables_shouldRequireExistingEndpointTables)
{
    database.createTable<collection_models::User>();

    EXPECT_ANY_THROW(database.createRelationTables<collection_models::User>());
}

TEST_P(CollectionRelationDatabaseTest, manyToManyRelationTableAndMutations_shouldBeIdempotentOnBothSides)
{
    database.createTable<collection_models::User>();
    database.createTable<collection_models::Role>();
    EXPECT_NO_THROW(database.createRelationTables<collection_models::User>());
    EXPECT_NO_THROW(database.createRelationTables<collection_models::User>());
    EXPECT_NO_THROW(database.deleteRelationTables<collection_models::Role>());

    const collection_models::User user{1, "user", {}};
    const collection_models::Role admin{10, "admin", {}};
    const collection_models::Role editor{20, "editor", {}};
    database.insert(user);
    database.insert(std::vector{admin, editor});

    EXPECT_EQ(database.link(user, "roles", admin), 1);
    EXPECT_EQ(database.link(user, "roles", admin), 0);
    EXPECT_EQ(database.link(admin, "users", user), 0);
    EXPECT_EQ(database.link(user, "roles", editor), 1);

    orm::Query<collection_models::User> included;
    included.include("roles").include("roles");
    auto users = database.select(included);

    ASSERT_EQ(users.size(), 1);
    EXPECT_TRUE(users[0].roles.isLoaded());
    EXPECT_EQ(sortedIds(users[0].roles), (std::vector<int>{10, 20}));

    EXPECT_EQ(database.unlink(admin, "users", user), 1);
    EXPECT_EQ(database.unlink(admin, "users", user), 0);

    orm::Query<collection_models::Role> inverseInclude;
    inverseInclude.include("users").orderBy(orm::query::asc(orm::query::col("id")));
    const auto roles = database.select(inverseInclude);

    ASSERT_EQ(roles.size(), 2);
    EXPECT_TRUE(roles[0].users.isLoaded());
    EXPECT_TRUE(roles[0].users.empty());
    EXPECT_TRUE(roles[1].users.isLoaded());
    ASSERT_EQ(roles[1].users.size(), 1);
    EXPECT_EQ(roles[1].users[0].id, user.id);
}

TEST_P(CollectionRelationDatabaseTest, manyToManyDeleteEndpoint_shouldDeleteOnlyJunctionRows)
{
    createUserRoleSchema();
    const collection_models::User user{1, "user", {}};
    const collection_models::Role role{10, "role", {}};
    database.insert(user);
    database.insert(role);
    ASSERT_EQ(database.link(user, "roles", role), 1);

    EXPECT_EQ(database.remove<collection_models::User>(orm::query::col("id") == user.id), 1);

    orm::Query<collection_models::Role> roleQuery;
    roleQuery.include("users");
    const auto roles = database.select(roleQuery);

    ASSERT_EQ(roles.size(), 1);
    EXPECT_EQ(roles[0].id, role.id);
    EXPECT_TRUE(roles[0].users.isLoaded());
    EXPECT_TRUE(roles[0].users.empty());
}

TEST_P(CollectionRelationDatabaseTest, manyToManyInclude_shouldShareOneTargetAcrossParentsWithoutDuplicates)
{
    createUserRoleSchema();
    const collection_models::User first{1, "first", {}};
    const collection_models::User second{2, "second", {}};
    const collection_models::Role shared{10, "shared", {}};
    database.insert(std::vector{first, second});
    database.insert(shared);
    ASSERT_EQ(database.link(first, "roles", shared), 1);
    ASSERT_EQ(database.link(second, "roles", shared), 1);

    orm::Query<collection_models::User> userQuery;
    userQuery.include("roles").orderBy(orm::query::asc(orm::query::col("id")));
    const auto users = database.select(userQuery);

    ASSERT_EQ(users.size(), 2);
    ASSERT_EQ(users[0].roles.size(), 1);
    ASSERT_EQ(users[1].roles.size(), 1);
    EXPECT_EQ(users[0].roles[0].id, shared.id);
    EXPECT_EQ(users[1].roles[0].id, shared.id);

    orm::Query<collection_models::Role> roleQuery;
    roleQuery.include("users");
    const auto roles = database.select(roleQuery);
    ASSERT_EQ(roles.size(), 1);
    EXPECT_EQ(sortedIds(roles[0].users), (std::vector<int>{1, 2}));
}

TEST_P(CollectionRelationDatabaseTest, insert_shouldIgnoreInMemoryCollectionAndNotCascadeSave)
{
    createUserRoleSchema();
    const collection_models::Role transientRole{10, "transient", {}};
    collection_models::User user{1, "user", {}};
    user.roles.values().push_back(transientRole);

    database.insert(user);

    orm::Query<collection_models::User> userQuery;
    userQuery.include("roles");
    const auto users = database.select(userQuery);
    ASSERT_EQ(users.size(), 1);
    EXPECT_TRUE(users[0].roles.isLoaded());
    EXPECT_TRUE(users[0].roles.empty());

    orm::Query<collection_models::Role> roleQuery;
    EXPECT_TRUE(database.select(roleQuery).empty());
}

TEST_P(CollectionRelationDatabaseTest, deleteRelationTables_shouldBeIdempotentOnOwningSide)
{
    createUserRoleSchema();

    EXPECT_NO_THROW(database.deleteRelationTables<collection_models::User>());
    EXPECT_NO_THROW(database.deleteRelationTables<collection_models::User>());
}

TEST_P(CollectionRelationDatabaseTest, include_shouldKeepCompleteCollectionsWhenRootQueryIsPaged)
{
    createUserRoleSchema();
    const collection_models::User first{1, "first", {}};
    const collection_models::User second{2, "second", {}};
    const collection_models::Role firstRole{10, "first-role", {}};
    const collection_models::Role secondRole{20, "second-role", {}};
    database.insert(std::vector{first, second});
    database.insert(std::vector{firstRole, secondRole});
    ASSERT_EQ(database.link(second, "roles", firstRole), 1);
    ASSERT_EQ(database.link(second, "roles", secondRole), 1);

    orm::Query<collection_models::User> query;
    query.include("roles").orderBy(orm::query::asc(orm::query::col("id"))).limit(1).offset(1);
    const auto users = database.select(query);

    ASSERT_EQ(users.size(), 1);
    EXPECT_EQ(users[0].id, second.id);
    EXPECT_TRUE(users[0].roles.isLoaded());
    EXPECT_EQ(sortedIds(users[0].roles), (std::vector<int>{10, 20}));
}

TEST_P(CollectionRelationDatabaseTest, include_shouldLoadSeveralCollectionsIndependently)
{
    database.createTable<collection_models::Member>();
    database.createTable<collection_models::Permission>();
    database.createTable<collection_models::Team>();
    database.createRelationTables<collection_models::Member>();
    const collection_models::Member member{1, "member", {}, {}};
    const collection_models::Permission permission{10, "write"};
    const collection_models::Team team{20, "core"};
    database.insert(member);
    database.insert(permission);
    database.insert(team);
    ASSERT_EQ(database.link(member, "permissions", permission), 1);
    ASSERT_EQ(database.link(member, "teams", team), 1);

    orm::Query<collection_models::Member> query;
    query.include("permissions").include("teams");
    const auto members = database.select(query);

    ASSERT_EQ(members.size(), 1);
    ASSERT_TRUE(members[0].permissions.isLoaded());
    ASSERT_EQ(members[0].permissions.size(), 1);
    EXPECT_EQ(members[0].permissions[0].id, permission.id);
    ASSERT_TRUE(members[0].teams.isLoaded());
    ASSERT_EQ(members[0].teams.size(), 1);
    EXPECT_EQ(members[0].teams[0].id, team.id);
}

TEST_P(CollectionRelationDatabaseTest, include_shouldHandleMoreParentsThanLegacySqliteParameterLimit)
{
    createUserRoleSchema();
    constexpr int parentCount = 1005;
    std::vector<collection_models::User> users;
    users.reserve(parentCount);
    for (int id = 1; id <= parentCount; ++id)
    {
        users.push_back({id, "user-" + std::to_string(id), {}});
    }
    database.insert(users);

    orm::Query<collection_models::User> query;
    query.include("roles");
    const auto returnedUsers = database.select(query);

    ASSERT_EQ(returnedUsers.size(), static_cast<std::size_t>(parentCount));
    EXPECT_TRUE(std::ranges::all_of(returnedUsers,
                                    [](const auto& user) { return user.roles.isLoaded() && user.roles.empty(); }));
}

TEST_P(CollectionRelationDatabaseTest, manyToMany_shouldSupportCompositeEndpointKeys)
{
    database.createTable<collection_models::CompositeOwner>();
    database.createTable<collection_models::CompositeTag>();
    database.createRelationTables<collection_models::CompositeOwner>();
    const collection_models::CompositeOwner owner{"tenant-a", 1, "owner", {}};
    const collection_models::CompositeTag firstTag{"scope-a", 10, "first"};
    const collection_models::CompositeTag secondTag{"scope-b", 20, "second"};
    database.insert(owner);
    database.insert(std::vector{firstTag, secondTag});

    EXPECT_EQ(database.link(owner, "tags", firstTag), 1);
    EXPECT_EQ(database.link(owner, "tags", secondTag), 1);

    orm::Query<collection_models::CompositeOwner> query;
    query.include("tags");
    const auto owners = database.select(query);

    ASSERT_EQ(owners.size(), 1);
    EXPECT_TRUE(owners[0].tags.isLoaded());
    EXPECT_EQ(sortedIds(owners[0].tags), (std::vector<int>{10, 20}));
    EXPECT_EQ(database.unlink(owner, "tags", firstTag), 1);
    EXPECT_EQ(database.unlink(owner, "tags", firstTag), 0);
}

TEST_P(CollectionRelationDatabaseTest, collectionPredicates_shouldFilterOneToManyWithoutLoadingIt)
{
    createAuthorBookSchema();
    const std::string injectedTitle{"x' OR 1=1 --"};
    const collection_models::Author matching{1, "matching", {}};
    const collection_models::Author empty{2, "empty", {}};
    const collection_models::Book book{10, injectedTitle, std::nullopt};
    database.insert(std::vector{matching, empty});
    database.insert(book);
    ASSERT_EQ(database.link(matching, "books", book), 1);

    orm::Query<collection_models::Author> anyQuery;
    anyQuery.where(orm::query::any("books", orm::query::col("title") == injectedTitle));
    const auto withMatchingBook = database.select(anyQuery);
    ASSERT_EQ(withMatchingBook.size(), 1);
    EXPECT_EQ(withMatchingBook[0].id, matching.id);
    EXPECT_FALSE(withMatchingBook[0].books.isLoaded());

    orm::Query<collection_models::Author> relatedPathQuery;
    relatedPathQuery.where(orm::query::any("books", orm::query::col("author.name") == matching.name));
    const auto matchedThroughBookAuthor = database.select(relatedPathQuery);
    ASSERT_EQ(matchedThroughBookAuthor.size(), 1);
    EXPECT_EQ(matchedThroughBookAuthor[0].id, matching.id);

    orm::Query<collection_models::Author> existsQuery;
    existsQuery.where(orm::query::exists("books"));
    const auto withBooks = database.select(existsQuery);
    ASSERT_EQ(withBooks.size(), 1);
    EXPECT_EQ(withBooks[0].id, matching.id);

    orm::Query<collection_models::Author> noneQuery;
    noneQuery.where(orm::query::none("books", orm::query::col("title") == injectedTitle));
    const auto withoutMatchingBook = database.select(noneQuery);
    ASSERT_EQ(withoutMatchingBook.size(), 1);
    EXPECT_EQ(withoutMatchingBook[0].id, empty.id);
}

TEST_P(CollectionRelationDatabaseTest, collectionPredicates_shouldFilterManyToMany)
{
    createUserRoleSchema();
    const collection_models::User adminUser{1, "admin-user", {}};
    const collection_models::User ordinaryUser{2, "ordinary-user", {}};
    const collection_models::User userWithoutRoles{3, "empty-user", {}};
    const collection_models::Role admin{10, "admin", {}};
    const collection_models::Role reader{20, "reader", {}};
    database.insert(std::vector{adminUser, ordinaryUser, userWithoutRoles});
    database.insert(std::vector{admin, reader});
    ASSERT_EQ(database.link(adminUser, "roles", admin), 1);
    ASSERT_EQ(database.link(ordinaryUser, "roles", reader), 1);

    orm::Query<collection_models::User> anyQuery;
    anyQuery.where(orm::query::any("roles", orm::query::col("name") == "admin"));
    const auto admins = database.select(anyQuery);
    ASSERT_EQ(admins.size(), 1);
    EXPECT_EQ(admins[0].id, adminUser.id);

    orm::Query<collection_models::User> existsQuery;
    existsQuery.where(orm::query::exists("roles"));
    const auto usersWithRoles = database.select(existsQuery);
    EXPECT_EQ(usersWithRoles.size(), 2);

    orm::Query<collection_models::User> noneQuery;
    noneQuery.where(orm::query::none("roles", orm::query::col("name") == "reader"));
    const auto usersWithoutReader = database.select(noneQuery);
    EXPECT_EQ(sortedIds(usersWithoutReader), (std::vector<int>{1, 3}));

    orm::Query<collection_models::Role> inverseQuery;
    inverseQuery.where(orm::query::any("users", orm::query::col("name") == ordinaryUser.name));
    const auto rolesWithOrdinaryUser = database.select(inverseQuery);
    ASSERT_EQ(rolesWithOrdinaryUser.size(), 1);
    EXPECT_EQ(rolesWithOrdinaryUser[0].id, reader.id);
}

TEST_P(CollectionRelationDatabaseTest, relationMutation_shouldParticipateInExplicitTransaction)
{
    createUserRoleSchema();
    const collection_models::User user{1, "user", {}};
    const collection_models::Role role{10, "role", {}};
    database.insert(user);
    database.insert(role);

    database.beginTransaction();
    ASSERT_EQ(database.link(user, "roles", role), 1);
    database.rollbackTransaction();

    orm::Query<collection_models::User> query;
    query.where(orm::query::exists("roles"));
    EXPECT_TRUE(database.select(query).empty());
}

TEST_P(CollectionRelationDatabaseTest, invalidCollectionPathsAndMissingKeys_shouldBeRejected)
{
    createUserRoleSchema();
    const collection_models::User missingKey{0, "missing-key", {}};
    const collection_models::User missingEndpoint{99, "missing-endpoint", {}};
    const collection_models::Role role{10, "role", {}};
    database.insert(role);

    EXPECT_THROW((void)database.link(missingKey, "roles", role), std::invalid_argument);
    EXPECT_ANY_THROW((void)database.link(missingEndpoint, "roles", role));

    orm::Query<collection_models::User> collectionPathQuery;
    collectionPathQuery.where(orm::query::col("roles.name") == "role");
    EXPECT_THROW((void)database.select(collectionPathQuery), std::invalid_argument);

    orm::Query<collection_models::User> collectionOrderQuery;
    collectionOrderQuery.orderBy(orm::query::asc(orm::query::col("roles.name")));
    EXPECT_THROW((void)database.select(collectionOrderQuery), std::invalid_argument);

    orm::Query<collection_models::User> nestedCollectionPredicate;
    nestedCollectionPredicate.where(
        orm::query::any("roles", orm::query::any("users", orm::query::col("name") == "nested")));
    EXPECT_THROW((void)database.select(nestedCollectionPredicate), std::invalid_argument);

    EXPECT_THROW(
        {
            orm::Query<collection_models::User> missingInclude;
            missingInclude.include("missing");
            (void)database.select(missingInclude);
        },
        std::invalid_argument);
}

TEST_P(CollectionRelationDatabaseTest, relationMutations_shouldRejectUnknownFieldsAndWrongTargetTypes)
{
    const collection_models::User user{1, "user", {}};
    const collection_models::Role role{10, "role", {}};
    const collection_models::Book book{20, "book", std::nullopt};

    EXPECT_THROW((void)database.link(user, "missing", role), std::invalid_argument);
    EXPECT_THROW((void)database.unlink(user, "missing", role), std::invalid_argument);
    EXPECT_THROW((void)database.link(user, "roles", book), std::invalid_argument);
    EXPECT_THROW((void)database.unlink(user, "roles", book), std::invalid_argument);
}

TEST_P(CollectionRelationDatabaseTest, includeOnEmptyRootResult_shouldNotRunCollectionHydration)
{
    createUserRoleSchema();
    orm::Query<collection_models::User> query;
    query.include("roles");

    EXPECT_TRUE(database.select(query).empty());
}

TEST_P(CollectionRelationDatabaseTest, includeOneOfSeveralCollections_shouldLeaveTheOtherUnloaded)
{
    database.createTable<collection_models::Member>();
    database.createTable<collection_models::Permission>();
    database.createTable<collection_models::Team>();
    database.createRelationTables<collection_models::Member>();
    database.insert(collection_models::Member{1, "member", {}, {}});

    orm::Query<collection_models::Member> query;
    query.include("permissions");
    const auto members = database.select(query);

    ASSERT_EQ(members.size(), 1);
    EXPECT_TRUE(members[0].permissions.isLoaded());
    EXPECT_FALSE(members[0].teams.isLoaded());
}

TEST_P(CollectionRelationDatabaseTest, relationEndpointValidation_shouldRejectMetadataKeySizeMismatch)
{
    auto& userInfo = orm::Model<collection_models::User>::getModelInfo();
    const auto savedInfo = userInfo;
    userInfo.columnsInfo.push_back(orm::model::ColumnInfo{.fieldName = "second_id",
                                                          .name = "second_id",
                                                          .type = orm::model::ColumnType::Int,
                                                          .isPrimaryKey = true,
                                                          .isForeignModel = false,
                                                          .isAutoIncrement = false,
                                                          .isUnique = false,
                                                          .isNotNull = true});
    userInfo.idColumnsNames.insert("second_id");

    const collection_models::User user{1, "user", {}};
    const collection_models::Role role{10, "role", {}};
    EXPECT_THROW((void)database.link(user, "roles", role), std::invalid_argument);

    userInfo = savedInfo;
}

TEST_P(CollectionRelationDatabaseTest, relationTableCreationForInverseMapping_shouldBeNoOp)
{
    EXPECT_NO_THROW(database.createRelationTables<collection_models::Role>());
}

TEST_P(CollectionRelationDatabaseTest, include_shouldRejectWrapperTargetMetadataMismatch)
{
    database.createTable<collection_models::User>();
    database.insert(collection_models::User{1, "user", {}});

    auto& userInfo = orm::Model<collection_models::User>::getModelInfo();
    const auto savedType = userInfo.relationsInfo.front().targetType;
    userInfo.relationsInfo.front().targetType = typeid(collection_models::Book);

    orm::Query<collection_models::User> query;
    query.include("roles");
    EXPECT_THROW((void)database.select(query), std::invalid_argument);

    userInfo.relationsInfo.front().targetType = savedType;
}

TEST(CollectionRelationDatabaseStandaloneTest, createRelationTablesWithoutSqliteConnection_shouldRejectBackend)
{
    orm::Database disconnected;
    EXPECT_THROW((void)disconnected.createRelationTables<collection_models::User>(), std::invalid_argument);
}

INSTANTIATE_TEST_SUITE_P(DatabaseTest, CollectionRelationDatabaseTest, connectionStrings);
