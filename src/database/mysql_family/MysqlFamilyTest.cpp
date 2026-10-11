module;

#include <future>

#include "NativeApi.hpp"
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

TEST(MysqlFamilyTest, categorizedDriverErrorsPreserveFallbackAndSanitizeMessages)
{
    class CategorizedError final : public soci::soci_error
    {
    public:
        explicit CategorizedError(error_category category) : soci_error{"password=secret"}, category_{category} {}
        auto get_error_category() const -> error_category override
        {
            return category_;
        }

    private:
        error_category category_;
    };
    const auto runtime = orm::db::mysql_family::makeRuntime(orm::db::BackendType::Mysql, "mysql://");
    for (const auto& [category, expected] :
         std::array<std::pair<soci::soci_error::error_category, orm::DatabaseErrorCode>, 4>{
             {{soci::soci_error::connection_error, orm::DatabaseErrorCode::Connection},
              {soci::soci_error::constraint_violation, orm::DatabaseErrorCode::Constraint},
              {soci::soci_error::unknown_transaction_state, orm::DatabaseErrorCode::Transaction},
              {soci::soci_error::unknown, orm::DatabaseErrorCode::Statement}}})
    {
        const CategorizedError native{category};
        const auto translated = runtime->translateError(native, orm::DatabaseErrorCode::Statement, "test statement");
        EXPECT_EQ(translated.getCode(), expected);
        EXPECT_FALSE(translated.getNativeCode().has_value());
        EXPECT_EQ(std::string_view{translated.what()}.find("secret"), std::string_view::npos);
        EXPECT_EQ(runtime->statementErrorInvalidatesTransaction(native),
                  category == soci::soci_error::connection_error);
    }
}

TEST(MysqlFamilyTest, reconnectOptionScannerHandlesSpacesEscapesAndMalformedOptions)
{
    const auto runtime = orm::db::mysql_family::makeRuntime(orm::db::BackendType::Mysql, "mysql://");
    soci::session session;
    for (const auto dsn : {"mysql:// \t reconnect = 0", "mysql://user = 'quoted\\'value' \t reconnect=1",
                           "mysql://password = 'back\\\\slash' db = test reconnect=0"})
        EXPECT_THROW(runtime->open(session, dsn), std::invalid_argument);
    EXPECT_THROW(runtime->open(session, "mysql://broken_option"), soci::soci_error);
    EXPECT_FALSE(session.is_connected());
}

TEST(MysqlFamilyTest, relationInsertSpecificationRejectsIncompleteMetadata)
{
    const auto owner = orm::modelView<collection_models::Schema, collection_models::User>();
    const auto relation = *owner.findRelation("roles");
    const orm::db::binding::PrimaryKey ownerKey{orm::query::QueryValue{1}};
    const orm::db::binding::PrimaryKey targetKey{orm::query::QueryValue{2}};
    const auto spec = orm::db::relations::linkInsertSpec(owner, relation, ownerKey, targetKey);
    EXPECT_EQ(spec.tableName, "collection_user_roles");
    EXPECT_EQ(spec.columns, (std::vector<std::string>{"user_id", "role_id"}));
    EXPECT_EQ(spec.conflictColumns, spec.columns);
    EXPECT_EQ(spec.valueExpressions, (std::vector<std::string>{":orm_rel_owner_0", ":orm_rel_target_0"}));

    auto invalid = relation;
    invalid.targetModelIndex = orm::model::noTargetModel;
    EXPECT_THROW((void)orm::db::relations::linkInsertSpec(owner, invalid, ownerKey, targetKey), std::invalid_argument);
    invalid = relation;
    invalid.kind = orm::model::RelationKind::ToOne;
    EXPECT_THROW((void)orm::db::relations::linkInsertSpec(owner, invalid, ownerKey, targetKey), std::invalid_argument);
    invalid = relation;
    invalid.junction = {};
    EXPECT_THROW((void)orm::db::relations::linkInsertSpec(owner, invalid, ownerKey, targetKey), std::invalid_argument);
    EXPECT_THROW((void)orm::db::relations::linkInsertSpec(owner, relation, {}, targetKey), std::invalid_argument);
    EXPECT_THROW((void)orm::db::relations::linkInsertSpec(owner, relation, ownerKey, {}), std::invalid_argument);
    invalid = relation;
    const std::array<std::string_view, 2> twoColumns{"user_id", "extra"};
    invalid.junction.ownerColumns = twoColumns;
    EXPECT_THROW((void)orm::db::relations::linkInsertSpec(owner, invalid, ownerKey, targetKey), std::invalid_argument);
    invalid = relation;
    invalid.junction.targetColumns = twoColumns;
    EXPECT_THROW((void)orm::db::relations::linkInsertSpec(owner, invalid, ownerKey, targetKey), std::invalid_argument);
}

TEST(MysqlFamilyTest, nativeClientOverridesRestoreNestedScopesExceptionsAndWorkerThreads)
{
    EXPECT_EQ(&orm_mysql_native::calls(), &orm_mysql_native::defaultCalls);
    orm_mysql_native::Calls outer;
    outer.errorNumber = +[](MYSQL*) -> unsigned int { return 7; };
    orm_mysql_native::Calls inner;
    inner.errorNumber = +[](MYSQL*) -> unsigned int { return 11; };
    {
        const orm_mysql_native::ScopedCalls outerScope{outer};
        EXPECT_EQ(orm_mysql_native::calls().errorNumber(nullptr), 7);
        auto worker = std::async(std::launch::async, [] { return &orm_mysql_native::calls(); });
        EXPECT_EQ(worker.get(), &orm_mysql_native::defaultCalls);
        EXPECT_THROW(
            [&]
            {
                const orm_mysql_native::ScopedCalls innerScope{inner};
                EXPECT_EQ(orm_mysql_native::calls().errorNumber(nullptr), 11);
                throw std::runtime_error{"failure inside override"};
            }(),
            std::runtime_error);
        EXPECT_EQ(orm_mysql_native::calls().errorNumber(nullptr), 7);
    }
    EXPECT_EQ(&orm_mysql_native::calls(), &orm_mysql_native::defaultCalls);
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
    auto runtime() -> const orm::db::BackendRuntime&
    {
        return orm::migrations::detail::DatabaseAccess::runtime(connection);
    }
    template <typename Operation>
    auto expectMigrationFailure(Operation&& operation, orm::migrations::ErrorCode expected,
                                std::optional<std::string> native = std::nullopt) -> void
    {
        try
        {
            std::forward<Operation>(operation)();
            FAIL() << "Expected a migration failure";
        }
        catch (const orm::migrations::MigrationError& error)
        {
            EXPECT_EQ(error.getCode(), expected);
            EXPECT_EQ(error.getBackendType(), GetParam().type);
            EXPECT_EQ(error.getNativeCode(), native);
        }
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

thread_local unsigned int migrationMetadataQueries{};
}

TEST_P(MysqlFamilyLinkTest, ClientCharsetSetupFailurePreservesItsNativeError)
{
    orm_mysql_native::Calls faults;
    faults.setCharacterSet = +[](MYSQL*, const char*) -> int { return 1; };
    faults.errorNumber = +[](MYSQL*) -> unsigned int { return 2019; };
    {
        const orm_mysql_native::ScopedCalls scope{faults};
        try
        {
            runtime().onConnect(session());
            FAIL() << "Expected the native charset setup failure";
        }
        catch (const soci::mysql_soci_error& error)
        {
            EXPECT_EQ(error.err_num_, 2019);
            EXPECT_EQ(runtime().translateError(error, orm::DatabaseErrorCode::Connection, "connect").getCode(),
                      orm::DatabaseErrorCode::Connection);
        }
    }
    EXPECT_TRUE(connection.isConnected());
}

TEST_P(MysqlFamilyLinkTest, ClientLinkLockErrorsPreserveNativeCauseAndRequireRollback)
{
    createEndpoints();
    for (const unsigned int number : {1064U, 2006U})
    {
        connection.beginTransaction();
        orm_mysql_native::Calls faults;
        faults.beforeStatement = number == 1064 ?
                                     +[](MYSQL*, std::string_view query)
        {
            if (query.starts_with("SELECT GET_LOCK"))
                throw soci::mysql_soci_error{"injected lock failure", 1064};
        } :
                                     +[](MYSQL*, std::string_view query)
        {
            if (query.starts_with("SELECT GET_LOCK"))
                throw soci::mysql_soci_error{"injected connection failure", 2006};
        };
        {
            const orm_mysql_native::ScopedCalls scope{faults};
            try
            {
                (void)link();
                FAIL() << "Expected the lock acquisition failure";
            }
            catch (const orm::DatabaseError& error)
            {
                EXPECT_EQ(error.getCode(),
                          number == 1064 ? orm::DatabaseErrorCode::Transaction : orm::DatabaseErrorCode::Connection);
                EXPECT_EQ(error.getNativeCode(), std::optional<std::string>{std::to_string(number)});
            }
        }
        EXPECT_THROW(connection.commitTransaction(), orm::DatabaseError);
        connection.rollbackTransaction();
        EXPECT_TRUE(connection.isConnected());
    }
    EXPECT_EQ(link(), 1);
}

TEST_P(MysqlFamilyLinkTest, ClientMigrationProtocolFailuresPreserveLockAndDrainResults)
{
    const auto history = runtime().migrationTable(session());
    runtime().createMigrationHistory(session(), history);
    runtime().beginMigration(session(), history);
    expectMigrationFailure(
        [&] { runtime().executeMigrationScript(session(), "SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='failed'"); },
        orm::migrations::ErrorCode::Execution, "1644");
    for (const int failure : {0, 1, 2})
    {
        orm_mysql_native::Calls faults;
        faults.errorNumber = +[](MYSQL*) -> unsigned int { return 1227; };
        if (failure == 0)
            faults.setServerOption = +[](MYSQL* handle, enum_mysql_set_option option) -> int
            {
                return option == MYSQL_OPTION_MULTI_STATEMENTS_ON ?
                           1 :
                           orm_mysql_native::defaultCalls.setServerOption(handle, option);
            };
        else if (failure == 1)
            faults.realQuery = +[](MYSQL*, const char*, unsigned long) -> int { return 1; };
        else
            faults.storeResult = +[](MYSQL* handle) -> MYSQL_RES*
            {
                auto* result = orm_mysql_native::defaultCalls.storeResult(handle);
                if (result != nullptr)
                    orm_mysql_native::defaultCalls.freeResult(result);
                return nullptr;
            };
        {
            const orm_mysql_native::ScopedCalls scope{faults};
            expectMigrationFailure([&] { runtime().executeMigrationScript(session(), "SELECT 1"); },
                                   orm::migrations::ErrorCode::Execution, "1227");
        }
        EXPECT_TRUE(connection.isConnected());
        int answer{};
        session() << "SELECT 42", soci::into(answer);
        EXPECT_EQ(answer, 42);
    }
    runtime().endMigration(session(), history);
}

TEST_P(MysqlFamilyLinkTest, ClientFailureDisablingMultiStatementsDisconnectsAfterCommittedDdl)
{
    const auto history = runtime().migrationTable(session());
    runtime().createMigrationHistory(session(), history);
    runtime().beginMigration(session(), history);
    orm_mysql_native::Calls faults;
    faults.errorNumber = +[](MYSQL*) -> unsigned int { return 1227; };
    faults.setServerOption = +[](MYSQL* handle, enum_mysql_set_option option) -> int
    {
        return option == MYSQL_OPTION_MULTI_STATEMENTS_OFF ?
                   1 :
                   orm_mysql_native::defaultCalls.setServerOption(handle, option);
    };
    {
        const orm_mysql_native::ScopedCalls scope{faults};
        expectMigrationFailure(
            [&] { runtime().executeMigrationScript(session(), "CREATE TABLE committed_before_failure(id INT)"); },
            orm::migrations::ErrorCode::Execution, "1227");
    }
    EXPECT_FALSE(session().is_connected());
    connection.disconnect();
    connection.connect(GetParam().type, testConnectionString());
    EXPECT_TRUE(runtime().tableExists(session(), "committed_before_failure"));
    runtime().beginMigration(session(), history);
    runtime().endMigration(session(), history);
}

TEST_P(MysqlFamilyLinkTest, HistoryCreationChecksThatTablesSurvivedBeforeReturning)
{
    const auto history = runtime().migrationTable(session());
    migrationMetadataQueries = 0;
    orm_mysql_native::Calls faults;
    faults.beforeStatement = +[](MYSQL* handle, std::string_view query)
    {
        if (query.starts_with("SELECT TABLE_TYPE") && ++migrationMetadataQueries == 3)
        {
            constexpr std::string_view drop = "DROP TABLE _orm_migrations";
            if (orm_mysql_native::defaultCalls.realQuery(handle, drop.data(),
                                                         static_cast<unsigned long>(drop.size())) != 0)
                throw soci::mysql_soci_error{"Could not emulate concurrent history removal", 1064};
        }
    };
    {
        const orm_mysql_native::ScopedCalls scope{faults};
        expectMigrationFailure([&] { runtime().createMigrationHistory(session(), history); },
                               orm::migrations::ErrorCode::InvalidHistory);
    }
    EXPECT_FALSE(runtime().migrationHistoryExists(session(), history));
    EXPECT_TRUE(runtime().migrationHistoryExists(session(), runtime().migrationJournalTable(session(), history)));
}

TEST_P(MysqlFamilyLinkTest, ReentrantMigrationLocksAreReleasedByDisconnectAndCanBeReacquired)
{
    const auto history = runtime().migrationTable(session());
    runtime().beginMigration(session(), history);
    runtime().beginMigration(session(), history);
    expectMigrationFailure([&] { runtime().endMigration(session(), history); },
                           orm::migrations::ErrorCode::LockUnavailable);
    EXPECT_FALSE(session().is_connected());
    connection.disconnect();
    connection.connect(GetParam().type, testConnectionString());
    runtime().beginMigration(session(), history);
    runtime().endMigration(session(), history);
}

TEST_P(MysqlFamilyLinkTest, NativeBackendForwardsMetadataProceduresAndUnsupportedFactories)
{
    session() << "CREATE TABLE native_identity(id BIGINT PRIMARY KEY AUTO_INCREMENT) ENGINE=InnoDB";
    session() << "INSERT INTO native_identity VALUES()";
    long long identity{};
    EXPECT_TRUE(session().get_last_insert_id("native_identity", identity));
    EXPECT_EQ(identity, 1);
    EXPECT_TRUE(session().get_dummy_from_table().empty());
    EXPECT_EQ(session().get_backend_name(), "mysql");
    std::vector<std::string> tables;
    soci::rowset<std::string> tableRows = (session().prepare << session().get_backend()->get_table_names_query());
    for (const auto& name : tableRows)
        tables.push_back(name);
    EXPECT_NE(std::ranges::find(tables, "native_identity"), tables.end());
    EXPECT_THROW((void)session().make_rowid_backend(), soci::soci_error);
    EXPECT_THROW((void)session().make_blob_backend(), soci::soci_error);

    std::unique_ptr<soci::details::statement_backend> statement{session().get_backend()->make_statement_backend()};
    statement->alloc();
    EXPECT_THROW((void)statement->execute(1), soci::soci_error);
    EXPECT_THROW((void)statement->prepare_for_describe(), soci::soci_error);
    statement->prepare("SELECT :first, ':ignored', :second", soci::details::st_repeatable_query);
    EXPECT_EQ(statement->get_parameter_name(0), "first");
    EXPECT_EQ(statement->get_parameter_name(1), "second");
    EXPECT_THROW((void)statement->get_parameter_name(2), std::out_of_range);
    EXPECT_EQ(statement->rewrite_for_procedure_call("ABS(-7)"), "select ABS(-7)");
    statement->clean_up();
}

TEST_P(MysqlFamilyLinkTest, ServerIdentityAndMalformedJunctionMappingsFailBeforeInsert)
{
    const auto other =
        GetParam().type == orm::db::BackendType::Mysql ? orm::db::BackendType::Mariadb : orm::db::BackendType::Mysql;
    const auto mismatched = orm::db::mysql_family::makeRuntime(other, "unused://");
    EXPECT_THROW(mismatched->onConnect(session()), std::invalid_argument);
    createEndpoints();
    const auto owner = orm::modelView<LinkSchema, collection_models::User>();
    const auto relation = *owner.findRelation("roles");
    const orm::db::binding::PrimaryKey key{orm::query::QueryValue{1}};
    orm::db::CommandGeneratorFactory factory;
    const auto statement =
        orm::db::relations::linkStatement(factory.getBackend(GetParam().type).dialect(), owner, relation, key, key);
    auto spec = orm::db::relations::linkInsertSpec(owner, relation, key, key);
    spec.valueExpressions.clear();
    EXPECT_THROW((void)runtime().executeInsertIfAbsent(session(), {statement.sql, statement.parameters}, spec),
                 orm::DatabaseError);
    spec = orm::db::relations::linkInsertSpec(owner, relation, key, key);
    spec.columns.pop_back();
    spec.valueExpressions.pop_back();
    EXPECT_THROW((void)runtime().executeInsertIfAbsent(session(), {statement.sql, statement.parameters}, spec),
                 std::invalid_argument);
    session() << "ALTER TABLE collection_user_roles DROP PRIMARY KEY, ADD PRIMARY KEY(user_id)";
    EXPECT_THROW((void)link(), orm::DatabaseError);
    int rows{};
    session() << "SELECT COUNT(*) FROM collection_user_roles", soci::into(rows);
    EXPECT_EQ(rows, 0);
}

TEST_P(MysqlFamilyLinkTest, FailedRollbackDisconnectsAndReleasesHeldLinkLocks)
{
    createEndpoints();
    connection.beginTransaction();
    EXPECT_EQ(link(), 1);
    int id{};
    session() << "SELECT CONNECTION_ID()", soci::into(id);
    orm::Database killer;
    killer.connect(GetParam().type, testConnectionString());
    orm::migrations::detail::DatabaseAccess::session(killer) << "KILL " + std::to_string(id);
    EXPECT_THROW(session().get_backend()->rollback(), soci::soci_error);
    EXPECT_FALSE(session().get_backend()->is_connected());
    EXPECT_NO_THROW(session().get_backend()->rollback());
    connection.disconnect();
    connection.connect(GetParam().type, testConnectionString());
    EXPECT_EQ(link(), 1);
}

TEST_P(MysqlFamilyLinkTest, MissingLinkLockOnCommitDisconnectsInsteadOfTrustingRelease)
{
    createEndpoints();
    connection.beginTransaction();
    EXPECT_EQ(link(), 1);
    int released{};
    session() << "SELECT RELEASE_ALL_LOCKS()", soci::into(released);
    EXPECT_EQ(released, 1);
    EXPECT_NO_THROW(session().get_backend()->commit());
    EXPECT_FALSE(session().get_backend()->is_connected());
    connection.disconnect();
    connection.connect(GetParam().type, testConnectionString());
    EXPECT_EQ(link(), 0);
}

TEST_P(MysqlFamilyLinkTest, ConnectionLossDuringJunctionWriteCleansUpTheAutocommitLock)
{
    createEndpoints();
    const auto owner = orm::modelView<LinkSchema, collection_models::User>();
    const auto relation = *owner.findRelation("roles");
    const orm::db::binding::PrimaryKey key{orm::query::QueryValue{1}};
    orm::db::CommandGeneratorFactory factory;
    auto statement =
        orm::db::relations::linkStatement(factory.getBackend(GetParam().type).dialect(), owner, relation, key, key);
    const auto spec = orm::db::relations::linkInsertSpec(owner, relation, key, key);
    int id{};
    session() << "SELECT CONNECTION_ID()", soci::into(id);
    statement.sql = "KILL " + std::to_string(id);
    EXPECT_THROW((void)runtime().executeInsertIfAbsent(session(), {statement.sql, statement.parameters}, spec),
                 soci::soci_error);
    EXPECT_FALSE(session().get_backend()->is_connected());
    connection.disconnect();
    connection.connect(GetParam().type, testConnectionString());
    EXPECT_EQ(link(), 1);
}

TEST_P(MysqlFamilyLinkTest, MigrationHooksRejectWrongNamespacesMissingLocksAndDeletedHistory)
{
    const auto history = runtime().migrationTable(session());
    EXPECT_THROW((void)runtime().migrationHistoryExists(session(), "`foreign`.`_orm_migrations`"),
                 orm::migrations::MigrationError);
    EXPECT_THROW(runtime().endMigration(session(), history), orm::migrations::MigrationError);
    runtime().createMigrationHistory(session(), history);
    runtime().beginMigration(session(), history);
    EXPECT_THROW(runtime().executeMigrationScript(session(), "DROP TABLE " + history), orm::migrations::MigrationError);
    EXPECT_FALSE(runtime().migrationHistoryExists(session(), history));
    runtime().endMigration(session(), history);
    EXPECT_THROW(runtime().executeMigrationScript(session(), "SELECT 1"), orm::migrations::MigrationError);
    EXPECT_FALSE(session().is_connected());
    connection.disconnect();
    connection.connect(GetParam().type, testConnectionString());
    EXPECT_TRUE(connection.isConnected());
}

TEST_P(MysqlFamilyLinkTest, MigrationHooksRequireAnExplicitDatabaseSelection)
{
    auto dsn = testConnectionString();
    const auto databaseOption = dsn.rfind("db=");
    ASSERT_NE(databaseOption, std::string::npos);
    dsn.erase(databaseOption);
    orm::Database noDatabase;
    noDatabase.connect(GetParam().type, dsn);
    auto& selectedSession = orm::migrations::detail::DatabaseAccess::session(noDatabase);
    const auto& selectedRuntime = orm::migrations::detail::DatabaseAccess::runtime(noDatabase);
    EXPECT_THROW((void)selectedRuntime.migrationTable(selectedSession), orm::migrations::MigrationError);
    EXPECT_THROW(selectedRuntime.validateMigrationSession(selectedSession), orm::migrations::MigrationError);
    EXPECT_TRUE(noDatabase.isConnected());
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
