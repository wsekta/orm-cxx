#include "tests/database/DatabaseTest.hpp"

#if ORM_CXX_ENABLE_POSTGRESQL_BACKEND && ORM_CXX_ENABLE_POSTGRESQL_INTEGRATION_TESTS

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "orm-cxx/database/postgresql/PostgresqlBackend.hpp"
#include "orm-cxx/model.hpp"
#include "tests/CollectionModelsDefinitions.hpp"

using namespace orm::query;

namespace postgresql_integration_models
{
struct AutoOnly
{
    int id;

    inline static constexpr orm::reflection::FixedString table_name{"postgresql_auto_only"};
    inline static constexpr auto auto_increment_columns = orm::autoIncrement<&AutoOnly::id>();
};

struct NameProjection
{
    std::string name;
};

struct GroupedProjection
{
    std::string name;
    long long users;
};

struct ExactSumProjection
{
    long long total;
};

struct LongGeneratedAlias
{
    int value;

    inline static constexpr orm::reflection::FixedString table_name{"tttttttttttttttttttttttttttttttttttttttt"};
    inline static constexpr orm::reflection::FixedString column_name{"cccccccccccccccccccccccc"};
    inline static constexpr auto columns_names =
        orm::columnNames(orm::columnName<&LongGeneratedAlias::value, column_name>());
};

static_assert(LongGeneratedAlias::table_name.size() == 40);
static_assert(LongGeneratedAlias::column_name.size() == 24);
} // namespace postgresql_integration_models

using PostgresqlIntegrationSchema =
    orm::Schema<models::ModelWithOneField, models::ModelWithOptional, models::ModelWithFloat, models::ModelWithAllInts,
                models::ModelWithAutoIncrementId, models::ModelWithId, models::ModelRelatedToOtherModel,
                collection_models::User, collection_models::Role, postgresql_integration_models::AutoOnly,
                postgresql_integration_models::LongGeneratedAlias>;

namespace
{
template <typename Operation>
auto expectPostgresqlError(Operation operation, orm::DatabaseErrorCode expectedCode, std::string_view operationName,
                           std::optional<std::string_view> sqlState) -> void
{
    try
    {
        operation();
        FAIL() << "Expected orm::DatabaseError";
    }
    catch (const orm::DatabaseError& error)
    {
        EXPECT_EQ(error.getCode(), expectedCode);
        EXPECT_EQ(error.getBackendType(), orm::db::BackendType::Postgres);
        EXPECT_EQ(error.getOperation(), operationName);
        if (sqlState.has_value())
        {
            ASSERT_TRUE(error.getNativeCode().has_value());
            EXPECT_EQ(error.getNativeCode().value(), sqlState.value());
        }
        else
        {
            EXPECT_FALSE(error.getNativeCode().has_value());
        }
    }
}

auto utf8TestValue() -> std::string
{
    return std::string{"Za"} + "\xC5\xBC" + "\xC3\xB3" + "\xC5\x82" + "\xC4\x87 g" + "\xC4\x99" + "\xC5\x9B" + "l" +
           "\xC4\x85 ja" + "\xC5\xBA\xC5\x84" + " x' OR 1=1 --";
}
} // namespace

class PostgresqlIntegrationTest : public DatabaseTest<PostgresqlIntegrationSchema>
{
};

TEST(PostgresqlTestSchemaTest, acceptsOnlyGeneratedPrefixAndSafeIdentifierCharacters)
{
    EXPECT_TRUE(postgresql_test::isSafeSchemaName("orm_cxx_test_0123456789abcdef"));
    EXPECT_FALSE(postgresql_test::isSafeSchemaName("public"));
    EXPECT_FALSE(postgresql_test::isSafeSchemaName("orm_cxx_test_"));
    EXPECT_FALSE(postgresql_test::isSafeSchemaName("other_schema"));
    EXPECT_FALSE(postgresql_test::isSafeSchemaName("orm_cxx_test_bad-name"));
    EXPECT_FALSE(postgresql_test::isSafeSchemaName("orm_cxx_test_bad\" CASCADE"));
    EXPECT_FALSE(postgresql_test::isSafeSchemaName(std::string(64, 'a')));
    EXPECT_THROW((void)postgresql_test::quotedSchemaName("public"), std::invalid_argument);
}

TEST(PostgresqlTestSchemaTest, explicitCleanupRemovesOnlyTheGeneratedSchema)
{
    const auto dsn = postgresql_test::configuredDsn();
    ASSERT_FALSE(dsn.empty())
        << "ORM_CXX_POSTGRESQL_TEST_DSN is required when PostgreSQL integration tests are enabled";

    postgresql_test::PostgresqlTestSchema schema{dsn};
    postgresql_test::PostgresqlTestSchema survivor{dsn};
    const auto schemaName = schema.schemaName();
    const auto survivorName = survivor.schemaName();
    auto verificationSession = soci::session{"postgresql", postgresql_test::connectionPayload(dsn)};
    int count{};
    verificationSession << "SELECT COUNT(*) FROM pg_namespace WHERE nspname = :schema_name", soci::use(schemaName),
        soci::into(count);
    ASSERT_EQ(count, 1);

    schema.drop();

    verificationSession << "SELECT COUNT(*) FROM pg_namespace WHERE nspname = :schema_name", soci::use(schemaName),
        soci::into(count);
    EXPECT_EQ(count, 0);

    verificationSession << "SELECT COUNT(*) FROM pg_namespace WHERE nspname = :schema_name", soci::use(survivorName),
        soci::into(count);
    EXPECT_EQ(count, 1);
    survivor.drop();
}

TEST_P(PostgresqlIntegrationTest, isolatedSearchPathDrivesRuntimeTableInspectionAndReconnect)
{
    auto* schema = postgresqlTestSchema();
    ASSERT_NE(schema, nullptr);

    const orm::db::postgresql::PostgresqlBackend backend;
    auto inspectionSession = soci::session{"postgresql", postgresql_test::connectionPayload(testConnectionString())};
    backend.runtime().onConnect(inspectionSession);

    std::string currentSchema;
    inspectionSession << "SELECT current_schema()", soci::into(currentSchema);
    EXPECT_EQ(currentSchema, schema->schemaName());

    constexpr auto tableName =
        orm::model::modelView<PostgresqlIntegrationSchema, models::ModelWithOneField>()->tableName;
    EXPECT_FALSE(backend.runtime().tableExists(inspectionSession, tableName));
    createTable<models::ModelWithOneField>();
    EXPECT_TRUE(backend.runtime().tableExists(inspectionSession, tableName));

    database.disconnect();
    database.connect(orm::db::BackendType::Postgres, testConnectionString());
    EXPECT_TRUE(database.isConnected());
    EXPECT_EQ(database.getBackendType(), orm::db::BackendType::Postgres);
    EXPECT_TRUE(backend.runtime().tableExists(inspectionSession, tableName));
}

TEST_P(PostgresqlIntegrationTest, rejectsGeneratedAliasesThatExceedThePostgresqlIdentifierLimit)
{
    orm::Query<postgresql_integration_models::LongGeneratedAlias> query;

    expectPostgresqlError([this, &query]() { (void)database.select(query); },
                          orm::DatabaseErrorCode::UnsupportedFeature, "select", std::nullopt);
}

TEST_P(PostgresqlIntegrationTest, scalarNullAndUtf8ValuesRoundTripAndUnsignedOverflowIsRejected)
{
    createTable<models::ModelWithOptional>();
    const auto text = utf8TestValue();
    database.insert(
        std::vector<models::ModelWithOptional>{{std::nullopt, std::nullopt, std::nullopt}, {42, text, 123.5}});

    orm::Query<models::ModelWithOptional> nullQuery;
    nullQuery.where(col("field1").isNull());
    const auto nullRows = database.select(nullQuery);
    ASSERT_EQ(nullRows.size(), 1);
    EXPECT_FALSE(nullRows[0].field1.has_value());
    EXPECT_FALSE(nullRows[0].field2.has_value());
    EXPECT_FALSE(nullRows[0].field3.has_value());

    orm::Query<models::ModelWithOptional> presentQuery;
    presentQuery.where(col("field1") == 42);
    const auto presentRows = database.select(presentQuery);
    ASSERT_EQ(presentRows.size(), 1);
    EXPECT_EQ(presentRows[0].field1, std::optional<int>{42});
    EXPECT_EQ(presentRows[0].field2, std::optional<std::string>{text});
    EXPECT_EQ(presentRows[0].field3, std::optional<double>{123.5});

    orm::Query<models::ModelWithOptional> orderedNullsQuery;
    orderedNullsQuery.orderBy(asc(col("field1")));
    const auto orderedNullRows = database.select(orderedNullsQuery);
    ASSERT_EQ(orderedNullRows.size(), 2);
    EXPECT_EQ(orderedNullRows[0].field1, std::optional<int>{42});
    EXPECT_FALSE(orderedNullRows[1].field1.has_value());

    createTable<models::ModelWithFloat>();
    database.insert(models::ModelWithFloat{7, "fraction", 0.1F});
    orm::Query<models::ModelWithFloat> floatQuery;
    const auto floatRows = database.select(floatQuery);
    ASSERT_EQ(floatRows.size(), 1);
    EXPECT_FLOAT_EQ(floatRows[0].field3, 0.1F);

    createTable<models::ModelWithAllInts>();
    const auto outsideBigintRange = static_cast<unsigned long long>(std::numeric_limits<long long>::max()) + 1ULL;
    expectPostgresqlError([this, outsideBigintRange]()
                          { database.insert(models::ModelWithAllInts{1, 2, 3, 4, 5, 6, 7, outsideBigintRange, 9}); },
                          orm::DatabaseErrorCode::Conversion, "insert", std::nullopt);
}

TEST_P(PostgresqlIntegrationTest, embeddedNulTextIsRejectedWithoutChangingStoredRows)
{
    createTable<models::ModelWithOptional>();
    database.insert(models::ModelWithOptional{1, "baseline", 1.0});

    const auto embeddedNul = std::string{"before"} + '\0' + "after";
    expectPostgresqlError([this, &embeddedNul]() { database.insert(models::ModelWithOptional{2, embeddedNul, 2.0}); },
                          orm::DatabaseErrorCode::Conversion, "insert", std::nullopt);

    orm::Query<models::ModelWithOptional> query;
    const auto rows = database.select(query);
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0].field1, std::optional<int>{1});
    EXPECT_EQ(rows[0].field2, std::optional<std::string>{"baseline"});
}

TEST_P(PostgresqlIntegrationTest, generatedIdentitySupportsDefaultValuesAndGeneratedKeysRemainUsable)
{
    createTable<postgresql_integration_models::AutoOnly>();
    database.insert(postgresql_integration_models::AutoOnly{0});
    database.insert(postgresql_integration_models::AutoOnly{0});

    orm::Query<postgresql_integration_models::AutoOnly> autoOnlyQuery;
    autoOnlyQuery.orderBy(asc(col("id")));
    const auto autoOnlyRows = database.select(autoOnlyQuery);
    ASSERT_EQ(autoOnlyRows.size(), 2);
    EXPECT_GT(autoOnlyRows[0].id, 0);
    EXPECT_GT(autoOnlyRows[1].id, autoOnlyRows[0].id);

    createTable<models::ModelWithAutoIncrementId>();
    database.insert(models::ModelWithAutoIncrementId{0, 10, "generated"});
    orm::Query<models::ModelWithAutoIncrementId> generatedQuery;
    const auto generatedRows = database.select(generatedQuery);
    ASSERT_EQ(generatedRows.size(), 1);

    orm::Query<models::ModelWithAutoIncrementId> selectedById;
    selectedById.where(col("id") == generatedRows[0].id);
    const auto selectedRows = database.select(selectedById);
    ASSERT_EQ(selectedRows.size(), 1);
    EXPECT_EQ(selectedRows[0].field2, "generated");
}

TEST_P(PostgresqlIntegrationTest, likePreservesPostgresqlCaseSensitivity)
{
    createTable<models::ModelWithId>();
    database.insert(models::ModelWithId{1, 10, "Alpha"});

    orm::Query<models::ModelWithId> lowercaseQuery;
    lowercaseQuery.where(col("field2").like("alpha%"));
    EXPECT_TRUE(database.select(lowercaseQuery).empty());

    orm::Query<models::ModelWithId> exactCaseQuery;
    exactCaseQuery.where(col("field2").like("Alpha%"));
    EXPECT_EQ(database.select(exactCaseQuery).size(), 1);
}

TEST_P(PostgresqlIntegrationTest, sumOfBigintProjectionPreservesValuesAboveDoublePrecision)
{
    constexpr auto twoToThe53 = 9'007'199'254'740'992LL;
    constexpr auto expectedSum = twoToThe53 + 1;

    createTable<models::ModelWithAllInts>();
    database.insert(std::vector<models::ModelWithAllInts>{
        {1, 2, 3, 4, 5, 6, twoToThe53, 8, 9},
        {2, 2, 3, 4, 5, 6, 1, 8, 9},
    });

    orm::ProjectionQuery<models::ModelWithAllInts, postgresql_integration_models::ExactSumProjection> query;
    query.project(as("total", sum(col("field7"))));
    const auto rows = database.select(query);

    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0].total, expectedSum);
}

TEST_P(PostgresqlIntegrationTest, relationUpsertIsIdempotentAndReportsAffectedRows)
{
    createTable<collection_models::User>();
    createTable<collection_models::Role>();
    createRelationTables<collection_models::User>();

    const auto user = collection_models::User{1, "user", {}};
    const auto role = collection_models::Role{2, "role", {}};
    database.insert(user);
    database.insert(role);

    EXPECT_EQ(database.link(user, "roles", role), 1);
    EXPECT_EQ(database.link(user, "roles", role), 0);
    EXPECT_EQ(database.unlink(user, "roles", role), 1);
    EXPECT_EQ(database.unlink(user, "roles", role), 0);
}

TEST_P(PostgresqlIntegrationTest, projectionDistinctGroupingOrderingAndPaginationCompose)
{
    createTable<models::ModelWithId>();
    database.insert(std::vector<models::ModelWithId>{
        {1, 10, "alpha"}, {2, 20, "alpha"}, {3, 30, "beta"}, {4, 40, "beta"}, {5, 50, "gamma"}});

    orm::ProjectionQuery<models::ModelWithId, postgresql_integration_models::NameProjection> distinctQuery;
    distinctQuery.project(as("name", col("field2"))).distinct().orderBy(desc(col("field2"))).offset(1).limit(2);
    const auto distinctRows = database.select(distinctQuery);
    ASSERT_EQ(distinctRows.size(), 2);
    EXPECT_EQ(distinctRows[0].name, "beta");
    EXPECT_EQ(distinctRows[1].name, "alpha");

    orm::ProjectionQuery<models::ModelWithId, postgresql_integration_models::NameProjection> invalidDistinctQuery;
    invalidDistinctQuery.project(as("name", col("field2"))).distinct().orderBy(asc(col("field1")));
    expectPostgresqlError([this, &invalidDistinctQuery]() { (void)database.select(invalidDistinctQuery); },
                          orm::DatabaseErrorCode::UnsupportedFeature, "select projection", std::nullopt);

    orm::ProjectionQuery<models::ModelWithId, postgresql_integration_models::GroupedProjection> groupedQuery;
    groupedQuery.project(as("name", col("field2")), as("users", countAll()))
        .groupBy(col("field2"))
        .having(countAll() >= 2)
        .orderBy(asc(col("field2")));
    const auto groupedRows = database.select(groupedQuery);
    ASSERT_EQ(groupedRows.size(), 2);
    EXPECT_EQ(groupedRows[0].name, "alpha");
    EXPECT_EQ(groupedRows[0].users, 2);
    EXPECT_EQ(groupedRows[1].name, "beta");
    EXPECT_EQ(groupedRows[1].users, 2);

    orm::ProjectionQuery<models::ModelWithId, postgresql_integration_models::GroupedProjection> invalidGroupingQuery;
    invalidGroupingQuery.project(as("name", col("field2")), as("users", countAll())).groupBy(col("field1"));
    expectPostgresqlError([this, &invalidGroupingQuery]() { (void)database.select(invalidGroupingQuery); },
                          orm::DatabaseErrorCode::UnsupportedFeature, "select projection", std::nullopt);
}

TEST_P(PostgresqlIntegrationTest, transactionRollbackRecoversAfterConstraintFailure)
{
    createTable<models::ModelWithId>();
    database.insert(models::ModelWithId{1, 10, "existing"});
    database.beginTransaction();

    expectPostgresqlError([this]() { database.insert(models::ModelWithId{1, 20, "duplicate"}); },
                          orm::DatabaseErrorCode::Constraint, "insert", "23505");
    expectPostgresqlError([this]() { database.commitTransaction(); }, orm::DatabaseErrorCode::Transaction,
                          "commit transaction", std::nullopt);
    EXPECT_NO_THROW(database.rollbackTransaction());
    EXPECT_NO_THROW(database.insert(models::ModelWithId{2, 20, "recovered"}));

    database.beginTransaction();
    database.insert(models::ModelWithId{3, 30, "committed"});
    database.commitTransaction();

    orm::Query<models::ModelWithId> query;
    query.orderBy(asc(col("id")));
    const auto rows = database.select(query);
    ASSERT_EQ(rows.size(), 3);
    EXPECT_EQ(rows[1].field2, "recovered");
    EXPECT_EQ(rows[2].field2, "committed");
}

TEST_P(PostgresqlIntegrationTest, nativeSqlStateDistinguishesUniqueAndForeignKeyConstraints)
{
    createTable<models::ModelWithId>();
    createTable<models::ModelRelatedToOtherModel>();
    database.insert(models::ModelWithId{1, 10, "target"});
    database.insert(models::ModelWithId{2, 20, "other"});

    expectPostgresqlError([this]() { database.insert(models::ModelWithId{1, 99, "duplicate"}); },
                          orm::DatabaseErrorCode::Constraint, "insert", "23505");

    const auto missingTarget = models::ModelWithId{999, 0, "missing"};
    expectPostgresqlError([this, &missingTarget]()
                          { database.insert(models::ModelRelatedToOtherModel{1, 100, "owner", missingTarget}); },
                          orm::DatabaseErrorCode::Constraint, "insert", "23503");

    orm::Query<models::ModelWithId> query;
    query.orderBy(asc(col("id")));
    EXPECT_EQ(database.select(query).size(), 2);
}

TEST_P(PostgresqlIntegrationTest, runtimeBindCeilingSupportsAPracticalLargePredicate)
{
    const orm::db::postgresql::PostgresqlBackend backend;
    auto inspectionSession = soci::session{"postgresql", postgresql_test::connectionPayload(testConnectionString())};
    backend.runtime().onConnect(inspectionSession);
    const auto limits = backend.runtime().limits(inspectionSession);
    ASSERT_TRUE(limits.maxBindParameters.has_value());

    constexpr std::size_t practicalParameterCount = 2'048;
    ASSERT_GE(limits.maxBindParameters.value(), practicalParameterCount);
    createTable<models::ModelWithId>();
    database.insert(models::ModelWithId{1, 10, "selected"});

    std::vector<int> ids(practicalParameterCount);
    std::iota(ids.begin(), ids.end(), 0);
    orm::Query<models::ModelWithId> query;
    query.where(col("id").in(ids));
    const auto rows = database.select(query);
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0].id, 1);
}

INSTANTIATE_TEST_SUITE_P(Postgresql, PostgresqlIntegrationTest, ::testing::Values(postgresqlBackendTestConfig),
                         backendTestName);

#endif
