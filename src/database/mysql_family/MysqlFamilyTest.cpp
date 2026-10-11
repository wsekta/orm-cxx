module;

#include <future>

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
import :migrations_internal;

TEST(MysqlFamilyTest, tokenizerPreservesQuotedIdentifiersStringsAndComments)
{
    const auto sql =
        "SELECT `a:b``c`, ':literal', \"x:y\" FROM `t` /* :comment */ WHERE `id`=:id # :line\n AND x=:id -- :last\n";
    const auto tokens = orm::db::mysql_family::tokenizeStatement(sql);
    EXPECT_EQ(tokens.names, (std::vector<std::string>{"id", "id"}));
    ASSERT_EQ(tokens.chunks.size(), 3);
    EXPECT_NE(tokens.chunks.front().find("`a:b``c`"), std::string::npos);
    EXPECT_EQ(tokens.chunks.back(), " -- :last\n");
    const auto assignment = orm::db::mysql_family::tokenizeStatement("SELECT @x:=1,:last");
    EXPECT_EQ(assignment.names, (std::vector<std::string>{"last"}));
    EXPECT_EQ(assignment.chunks[0], "SELECT @x:=1,");
    const auto carriageReturn = orm::db::mysql_family::tokenizeStatement("SELECT 1 # comment\r,:value");
    EXPECT_EQ(carriageReturn.names, (std::vector<std::string>{"value"}));
}

TEST(MysqlFamilyTest, tokenizerRejectsMalformedOrExecutableFragments)
{
    for (const auto sql :
         {"SELECT 'x", "SELECT `x", "SELECT /* x", "SELECT :!", "SELECT /*! :x */", "SELECT /*M! :x */"})
        EXPECT_THROW((void)orm::db::mysql_family::tokenizeStatement(sql), std::invalid_argument);
    const auto escaped = orm::db::mysql_family::tokenizeStatement("SELECT 'a\\\':fake',:real");
    EXPECT_EQ(escaped.names, (std::vector<std::string>{"real"}));
}

TEST(MysqlFamilyTest, emptyMigrationScriptsAcceptInertCommentsAndSeparators)
{
    for (const auto sql : {"", " ; \n", "/* comment */;# line\n-- second\n", "# trailing", "-- trailing", "#\n"})
        EXPECT_TRUE(orm::db::mysql_family::isEmptyScript(sql));
    for (const auto sql : {"SELECT 1", "/* unfinished", "/*! SELECT 1 */", "/*M! SELECT 1 */", "--not_a_comment",
                           "'text'", "#\nSELECT 1", "#\rSELECT 1"})
        EXPECT_FALSE(orm::db::mysql_family::isEmptyScript(sql));
}

namespace
{
struct OrderedRecord
{
    int id;
    std::string value;
    inline static constexpr orm::reflection::FixedString table_name{"mysql_family_ordered"};
    inline static constexpr auto string_lengths = orm::stringLengths(orm::stringLength<&OrderedRecord::value, 1500>());
};
using LinkSchema = orm::Schema<collection_models::User, collection_models::Role, OrderedRecord>;

class MysqlFamilyLinkTest : public DatabaseTest<LinkSchema>
{
public:
    auto createEndpoints() -> void
    {
        createTable<collection_models::User>();
        createTable<collection_models::Role>();
        createRelationTables<collection_models::User>();
        database.insert(collection_models::User{.id = 1, .name = "user", .roles = {}});
        for (int id = 1; id <= 3; ++id)
            database.insert(collection_models::Role{.id = id, .name = "role", .users = {}});
    }
    auto session() -> soci::session&
    {
        return orm::migrations::detail::DatabaseAccess::session(connection);
    }
    auto link(int role = 1) -> std::size_t
    {
        return database.link<&collection_models::User::roles>(
            collection_models::User{.id = 1, .name = {}, .roles = {}},
            collection_models::Role{.id = role, .name = {}, .users = {}});
    }
    auto startLink(std::shared_future<void> start) -> std::future<std::size_t>
    {
        std::promise<void> ready;
        auto readyFuture = ready.get_future();
        auto result =
            std::async(std::launch::async,
                       [type = GetParam().type, dsn = testConnectionString(), start, ready = std::move(ready)]() mutable
                       {
                           orm::Database second;
                           try
                           {
                               second.connect(type, dsn);
                               ready.set_value();
                           }
                           catch (...)
                           {
                               ready.set_exception(std::current_exception());
                               throw;
                           }
                           start.wait();
                           return second.orm<LinkSchema>().link<&collection_models::User::roles>(
                               collection_models::User{.id = 1, .name = {}, .roles = {}},
                               collection_models::Role{.id = 1, .name = {}, .users = {}});
                       });
        readyFuture.get();
        return result;
    }
};
}

TEST_P(MysqlFamilyLinkTest, ConcurrentAutocommitLinksReturnOneAndZero)
{
    createEndpoints();
    std::promise<void> start;
    auto second = startLink(start.get_future().share());
    start.set_value();
    const auto firstResult = link();
    const auto secondResult = second.get();
    EXPECT_EQ(firstResult + secondResult, 1);
    EXPECT_LE(firstResult, 1);
    EXPECT_LE(secondResult, 1);
}

TEST_P(MysqlFamilyLinkTest, NativeVectorBindingsExecuteOnceAndKeepStatementAffectedRows)
{
    createTable<OrderedRecord>();
    std::vector<int> ids{1, 2, 3};
    std::vector<std::string> values{"first", "second", "third"};
    soci::statement insert =
        (session().prepare << "INSERT INTO mysql_family_ordered (`id`, `value`) VALUES (:id, :value)",
         soci::use(ids, "id"), soci::use(values, "value"));
    insert.execute(true);
    EXPECT_EQ(insert.get_affected_rows(), 3);
    EXPECT_TRUE(connection.isConnected());
    EXPECT_EQ(insert.get_affected_rows(), 3);

    int first = 1;
    std::vector<int> readIds(3);
    std::vector<std::string> readValues(3);
    soci::statement select =
        (session().prepare << "SELECT `id`, `value` FROM mysql_family_ordered WHERE `id` >= :first ORDER BY `id`",
         soci::use(first, "first"), soci::into(readIds), soci::into(readValues));
    ASSERT_TRUE(select.execute(true));
    EXPECT_EQ(readIds, ids);
    EXPECT_EQ(readValues, values);
    EXPECT_FALSE(select.fetch());
    EXPECT_EQ(insert.get_affected_rows(), 3);

    int scalar{};
    soci::statement invalid = (session().prepare << "SELECT :id", soci::use(ids, "id"), soci::into(scalar));
    EXPECT_THROW(invalid.execute(true), soci::soci_error);
}

TEST_P(MysqlFamilyLinkTest, BoundedLongStringsSortBeyondTheDefaultServerPrefix)
{
    createTable<OrderedRecord>();
    const std::string prefix(1100, 'a');
    database.insert(OrderedRecord{1, prefix + "b"});
    database.insert(OrderedRecord{2, prefix + "a"});
    orm::Query<OrderedRecord> query;
    query.orderBy(orm::query::asc(orm::query::col<&OrderedRecord::value>()));
    const auto values = database.select(query);
    ASSERT_EQ(values.size(), 2);
    EXPECT_EQ(values[0].id, 2);
    EXPECT_EQ(values[1].id, 1);
    if (GetParam().type == orm::db::BackendType::Mariadb)
    {
        int maxSort{};
        session() << "SELECT @@SESSION.max_sort_length", soci::into(maxSort);
        EXPECT_EQ(maxSort, 65536);
    }
}

TEST_P(MysqlFamilyLinkTest, ConcurrentLinksObserveTransactionCommitAndRollback)
{
    createEndpoints();
    for (const bool commit : {true, false})
    {
        connection.beginTransaction();
        EXPECT_EQ(link(), 1);
        std::promise<void> start;
        auto second = startLink(start.get_future().share());
        start.set_value();
        EXPECT_EQ(second.wait_for(std::chrono::milliseconds{100}), std::future_status::timeout);
        if (commit)
            connection.commitTransaction();
        else
            connection.rollbackTransaction();
        EXPECT_EQ(second.get(), commit ? 0 : 1);
        EXPECT_EQ(
            database.unlink<&collection_models::User::roles>(collection_models::User{.id = 1, .name = {}, .roles = {}},
                                                             collection_models::Role{.id = 1, .name = {}, .users = {}}),
            1);
    }
}

TEST_P(MysqlFamilyLinkTest, OtherUniqueConstraintsAndTriggerDuplicateErrorsArePropagated)
{
    createEndpoints();
    EXPECT_EQ(link(), 1);
    session() << "ALTER TABLE collection_user_roles ADD CONSTRAINT only_one_role UNIQUE(user_id)";
    const auto expectConstraint = [this]
    {
        try
        {
            (void)link(2);
            FAIL() << "Expected the unrelated constraint failure";
        }
        catch (const orm::DatabaseError& error)
        {
            EXPECT_EQ(error.getCode(), orm::DatabaseErrorCode::Constraint);
            EXPECT_EQ(error.getOperation(), "link relation");
        }
    };
    expectConstraint();
    session() << "ALTER TABLE collection_user_roles DROP INDEX only_one_role";
    session() << "CREATE TABLE other_primary(id INT PRIMARY KEY) ENGINE=InnoDB";
    session() << "INSERT INTO other_primary VALUES(1)";
    session() << "CREATE TRIGGER link_fails BEFORE INSERT ON collection_user_roles FOR EACH ROW "
                 "INSERT INTO other_primary VALUES(1)";
    expectConstraint();
    EXPECT_EQ(link(), 0);
    session() << "DROP TRIGGER link_fails";
    orm::Database second;
    second.connect(GetParam().type, testConnectionString());
    EXPECT_EQ(second.orm<LinkSchema>().link<&collection_models::User::roles>(
                  collection_models::User{.id = 1, .name = {}, .roles = {}},
                  collection_models::Role{.id = 2, .name = {}, .users = {}}),
              1);
}

TEST_P(MysqlFamilyLinkTest, LinkLockTimeoutRequiresRollbackAndDisconnectReleasesLocks)
{
    createEndpoints();
    orm::Database second;
    second.connect(GetParam().type, testConnectionString());
    second.beginTransaction();
    EXPECT_EQ(second.orm<LinkSchema>().link<&collection_models::User::roles>(
                  collection_models::User{.id = 1, .name = {}, .roles = {}},
                  collection_models::Role{.id = 1, .name = {}, .users = {}}),
              1);
    connection.beginTransaction();
    try
    {
        (void)link();
        FAIL() << "Expected a link lock timeout";
    }
    catch (const orm::DatabaseError& error)
    {
        EXPECT_EQ(error.getCode(), orm::DatabaseErrorCode::Transaction);
        EXPECT_EQ(error.getOperation(), "link relation");
    }
    orm::Query<collection_models::User> blockedQuery;
    EXPECT_THROW((void)database.select(blockedQuery), orm::DatabaseError);
    EXPECT_THROW(connection.commitTransaction(), orm::DatabaseError);
    connection.rollbackTransaction();
    second.disconnect();
    EXPECT_EQ(link(), 1);
}

TEST_P(MysqlFamilyLinkTest, NativeDeadlockBlocksTheVictimUntilRollback)
{
    createEndpoints();
    database.insert(collection_models::User{.id = 2, .name = "second", .roles = {}});
    orm::Database second;
    second.connect(GetParam().type, testConnectionString());
    auto secondContext = second.orm<LinkSchema>();
    const auto updateName = [](auto& context, int id, std::string name)
    {
        orm::Update<collection_models::User> update;
        update.set(orm::query::col<&collection_models::User::name>(), std::move(name))
            .where(orm::query::col<&collection_models::User::id>() == id);
        return context.update(update);
    };
    connection.beginTransaction();
    second.beginTransaction();
    EXPECT_EQ(updateName(database, 1, "first lock"), 1);
    EXPECT_EQ(updateName(secondContext, 2, "second lock"), 1);
    std::promise<void> started;
    auto startedFuture = started.get_future();
    auto worker = std::async(std::launch::async,
                             [&]() -> std::optional<orm::DatabaseError>
                             {
                                 started.set_value();
                                 try
                                 {
                                     (void)updateName(secondContext, 1, "worker");
                                     return std::nullopt;
                                 }
                                 catch (const orm::DatabaseError& error)
                                 {
                                     return error;
                                 }
                             });
    startedFuture.get();
    std::optional<orm::DatabaseError> firstError;
    try
    {
        (void)updateName(database, 2, "main");
    }
    catch (const orm::DatabaseError& error)
    {
        firstError = error;
    }
    catch (...)
    {
        connection.rollbackTransaction();
        throw;
    }
    const auto secondError = worker.get();
    EXPECT_NE(firstError.has_value(), secondError.has_value());
    if (firstError || secondError)
    {
        const auto& error = firstError ? *firstError : *secondError;
        EXPECT_EQ(error.getCode(), orm::DatabaseErrorCode::Transaction);
        EXPECT_EQ(error.getNativeCode(), std::optional<std::string>{"1213"});
        auto& victim = firstError ? connection : second;
        auto victimContext = victim.orm<LinkSchema>();
        orm::Query<collection_models::User> blockedQuery;
        EXPECT_THROW((void)victimContext.select(blockedQuery), orm::DatabaseError);
        EXPECT_THROW((void)updateName(victimContext, 1, "blocked"), orm::DatabaseError);
        EXPECT_THROW(victim.commitTransaction(), orm::DatabaseError);
    }
    connection.rollbackTransaction();
    second.rollbackTransaction();
    orm::Query<collection_models::User> recoveredQuery;
    EXPECT_EQ(database.select(recoveredQuery).size(), 2);
    EXPECT_EQ(secondContext.select(recoveredQuery).size(), 2);
    EXPECT_EQ(updateName(database, 1, "recovered"), 1);
}

INSTANTIATE_TEST_SUITE_P(DatabaseTest, MysqlFamilyLinkTest,
                         ::testing::ValuesIn(
                             []
                             {
                                 std::vector<BackendTestConfig> result;
                                 if constexpr (orm::test::config::mysqlIntegrationEnabled)
                                     result.push_back({orm::db::BackendType::Mysql, "Mysql", {}, true});
                                 if constexpr (orm::test::config::mariadbIntegrationEnabled)
                                     result.push_back({orm::db::BackendType::Mariadb, "Mariadb", {}, true});
                                 return result;
                             }()),
                         orm::test::fixtures::backendTestName);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(MysqlFamilyLinkTest);
