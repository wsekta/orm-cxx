#include "tests/UnitTestPrelude.hpp"

import orm;

#include "tests/CollectionModelsDefinitions.hpp"
#include "tests/database/DatabaseTest.hpp"

using namespace orm::test::fixtures;

using namespace orm::query;

namespace conformance_models
{
struct Projection
{
    int id;
    std::string name;
};

struct NarrowProjection
{
    unsigned char value;
};

struct GroupedProjection
{
    std::string name;
    long long users;
};

struct NullableAggregateProjection
{
    std::optional<double> averageValue;
};

struct ReservedIdentifierModel
{
    int id;
    std::string value;

    inline static constexpr orm::reflection::FixedString table_name{"order"};
    inline static constexpr auto columns_names =
        orm::columnNames(orm::columnName<&ReservedIdentifierModel::id, "select">(),
                         orm::columnName<&ReservedIdentifierModel::value, "group">());
};

struct MappedModel
{
    int id;
    std::string name;

    inline static constexpr orm::reflection::FixedString table_name{"conformance_mapped_table"};
    inline static constexpr auto columns_names = orm::columnNames(orm::columnName<&MappedModel::id, "mapped_id">(),
                                                                  orm::columnName<&MappedModel::name, "mapped_name">());
};

struct NonPortableBindNameModel
{
    int id;
    std::string value;

    inline static constexpr auto columns_names =
        orm::columnNames(orm::columnName<&NonPortableBindNameModel::value, "odd-name">());
};

struct TypedAccount
{
    int id;
    std::optional<int> age;
    std::optional<std::string> email;
    std::optional<MappedModel> profile;
    orm::ManyToMany<collection_models::Role> roles;

    inline static constexpr orm::reflection::FixedString table_name{"typed_accounts"};
    inline static constexpr auto columns_names =
        orm::columnNames(orm::columnName<&TypedAccount::age, "account_age">(),
                         orm::columnName<&TypedAccount::profile, "account_profile">());
    inline static constexpr auto relations = orm::relations(orm::manyToMany<&TypedAccount::roles>()
                                                                .through<"typed_account_roles">()
                                                                .ownerColumns<"account_id">()
                                                                .targetColumns<"role_id">());
};

struct TypedAccountStats
{
    std::optional<std::string> profile;
    long long accounts;
    std::optional<long long> totalAge;
    std::optional<double> averageAge;
    std::optional<int> minimumAge;
    std::optional<int> maximumAge;
};

struct AliasProfile
{
    int id;
    std::string name;

    inline static constexpr orm::reflection::FixedString table_name{"conformance_alias_profiles"};
};

struct AliasTarget
{
    int id;
    std::string name;
    std::optional<AliasProfile> profile;

    inline static constexpr orm::reflection::FixedString table_name{"conformance_alias_targets"};
    inline static constexpr auto columns_names =
        orm::columnNames(orm::columnName<&AliasTarget::profile, "conformance_alias_owner">());
};

struct AliasOwner
{
    int id;
    std::string name;
    orm::ManyToMany<AliasTarget> targets;

    inline static constexpr orm::reflection::FixedString table_name{"conformance_alias_owner"};
    inline static constexpr auto relations =
        orm::relations(orm::manyToMany<&AliasOwner::targets>().through<"conformance_alias_links">());
};

using Schema =
    orm::Schema<models::SomeDataModel, models::ModelWithOptional, models::ModelWithId, models::ModelWithAutoIncrementId,
                models::ModelWithOverwrittenId, models::ModelRelatedToOtherModel,
                models::ModelOptionallyRelatedToOtherModel, models::ModelWithAllBasicTypes, collection_models::Author,
                collection_models::Book, collection_models::User, collection_models::Role,
                collection_models::CompositeOwner, collection_models::CompositeTag, ReservedIdentifierModel,
                MappedModel, NonPortableBindNameModel, TypedAccount, AliasProfile, AliasTarget, AliasOwner>;
} // namespace conformance_models

namespace
{
template <typename Operation>
auto expectDatabaseError(Operation operation, orm::DatabaseErrorCode expectedCode, orm::db::BackendType expectedBackend,
                         const char* expectedOperation) -> void
{
    try
    {
        operation();
        FAIL() << "Expected orm::DatabaseError";
    }
    catch (const orm::DatabaseError& error)
    {
        EXPECT_EQ(error.getCode(), expectedCode);
        EXPECT_EQ(error.getBackendType(), expectedBackend);
        EXPECT_EQ(error.getOperation(), expectedOperation);
    }
}

auto supportsSomeDataModel(const orm::db::BackendCapabilities& capabilities) -> bool
{
    return capabilities.supportsColumnType(orm::model::ColumnType::Int) and
           capabilities.supportsColumnType(orm::model::ColumnType::Double) and
           capabilities.supportsColumnType(orm::model::ColumnType::String);
}

auto supportsModelWithId(const orm::db::BackendCapabilities& capabilities) -> bool
{
    return capabilities.supportsColumnType(orm::model::ColumnType::Int) and
           capabilities.supportsColumnType(orm::model::ColumnType::String);
}

auto supportsAllBasicTypes(const orm::db::BackendCapabilities& capabilities) -> bool
{
    constexpr auto requiredTypes = std::array{
        orm::model::ColumnType::Bool,
        orm::model::ColumnType::Char,
        orm::model::ColumnType::UnsignedChar,
        orm::model::ColumnType::Short,
        orm::model::ColumnType::UnsignedShort,
        orm::model::ColumnType::Int,
        orm::model::ColumnType::UnsignedInt,
        orm::model::ColumnType::LongLong,
        orm::model::ColumnType::UnsignedLongLong,
        orm::model::ColumnType::Float,
        orm::model::ColumnType::Double,
        orm::model::ColumnType::String,
    };

    return std::ranges::all_of(requiredTypes,
                               [&capabilities](const auto type) { return capabilities.supportsColumnType(type); });
}

auto canCreateAndDropSomeDataModel(const orm::db::BackendCapabilities& capabilities) -> bool
{
    return supportsSomeDataModel(capabilities) and capabilities.schema.createTableIfNotExists and
           capabilities.schema.dropTableIfExists;
}

auto canPopulateSomeDataModel(const orm::db::BackendCapabilities& capabilities) -> bool
{
    return canCreateAndDropSomeDataModel(capabilities) and capabilities.mutations.insert;
}

auto canPopulateModelWithId(const orm::db::BackendCapabilities& capabilities) -> bool
{
    return supportsModelWithId(capabilities) and capabilities.schema.createTableIfNotExists and
           capabilities.schema.dropTableIfExists and capabilities.mutations.insert;
}
} // namespace

class BackendConformanceTest : public DatabaseTest<conformance_models::Schema>
{
};

TEST_P(BackendConformanceTest, connectionReportsSelectedBackendAndCoreCapabilities)
{
    ASSERT_TRUE(connection.isConnected());
    EXPECT_EQ(connection.getBackendType(), GetParam().type);

    const auto& capabilities = connection.getBackendCapabilities();
    EXPECT_EQ(&capabilities, &connection.getBackendCapabilities());
}

TEST_P(BackendConformanceTest, supportedBackendAdvertisesRequiredCoreCapabilities)
{
    if (not GetParam().supported)
    {
        GTEST_SKIP() << "Experimental backends may advertise a partial capability profile";
    }

    const auto& capabilities = connection.getBackendCapabilities();

    EXPECT_TRUE(capabilities.schema.createTableIfNotExists);
    EXPECT_TRUE(capabilities.schema.dropTableIfExists);
    EXPECT_TRUE(capabilities.mutations.insert);
    EXPECT_TRUE(capabilities.mutations.update);
    EXPECT_TRUE(capabilities.mutations.remove);
    EXPECT_EQ(capabilities.mutations.affectedRows, orm::db::AffectedRowsSupport::Reliable);
    EXPECT_TRUE(capabilities.transactions);
    EXPECT_TRUE(capabilities.supportsColumnType(orm::model::ColumnType::Int));
    EXPECT_TRUE(capabilities.supportsColumnType(orm::model::ColumnType::Double));
    EXPECT_TRUE(capabilities.supportsColumnType(orm::model::ColumnType::String));
}

TEST_P(BackendConformanceTest, connectionStringAutoDetectionSelectsConfiguredBackend)
{
    orm::Database detectedDatabaseConnection;

    detectedDatabaseConnection.connect(testConnectionString());

    EXPECT_TRUE(detectedDatabaseConnection.isConnected());
    EXPECT_EQ(detectedDatabaseConnection.getBackendType(), GetParam().type);
    detectedDatabaseConnection.disconnect();
}

TEST_P(BackendConformanceTest, connectedDatabaseRejectsSecondConnect)
{
    expectDatabaseError([this]() { connection.connect(testConnectionString()); },
                        orm::DatabaseErrorCode::AlreadyConnected, GetParam().type, "connect");
    EXPECT_TRUE(connection.isConnected());
}

TEST_P(BackendConformanceTest, disconnectedDatabaseRejectsCapabilitiesAndOperations)
{
    orm::Database disconnectedDatabaseConnection;
    orm::OrmContext<conformance_models::Schema> disconnectedDatabase =
        disconnectedDatabaseConnection.orm<conformance_models::Schema>();

    expectDatabaseError([&disconnectedDatabaseConnection]()
                        { (void)disconnectedDatabaseConnection.getBackendCapabilities(); },
                        orm::DatabaseErrorCode::NotConnected, orm::db::BackendType::Empty, "database operation");
    expectDatabaseError([&disconnectedDatabase]() { disconnectedDatabase.createTable<models::SomeDataModel>(); },
                        orm::DatabaseErrorCode::NotConnected, orm::db::BackendType::Empty, "database operation");
}

TEST_P(BackendConformanceTest, disconnectAllowsReconnect)
{
    connection.disconnect();
    EXPECT_FALSE(connection.isConnected());
    EXPECT_EQ(connection.getBackendType(), orm::db::BackendType::Empty);

    connection.connect(GetParam().type, testConnectionString());

    EXPECT_TRUE(connection.isConnected());
    EXPECT_EQ(connection.getBackendType(), GetParam().type);
}

TEST_P(BackendConformanceTest, tableLifecycleIsIdempotent)
{
    const auto& capabilities = connection.getBackendCapabilities();

    if (not capabilities.schema.createTableIfNotExists or not supportsSomeDataModel(capabilities))
    {
        expectDatabaseError([this]() { database.createTable<models::SomeDataModel>(); },
                            orm::DatabaseErrorCode::UnsupportedFeature, GetParam().type, "create table");
    }

    if (not capabilities.schema.dropTableIfExists)
    {
        expectDatabaseError([this]() { database.deleteTable<models::SomeDataModel>(); },
                            orm::DatabaseErrorCode::UnsupportedFeature, GetParam().type, "drop table");
    }

    if (not canCreateAndDropSomeDataModel(capabilities))
    {
        return;
    }

    createTable<models::SomeDataModel>();

    EXPECT_NO_THROW(database.createTable<models::SomeDataModel>());
    EXPECT_NO_THROW(database.deleteTable<models::SomeDataModel>());
    EXPECT_NO_THROW(database.deleteTable<models::SomeDataModel>());
}

TEST_P(BackendConformanceTest, crudRoundTripsSupportedScalarValues)
{
    const auto& capabilities = connection.getBackendCapabilities();

    if (not canCreateAndDropSomeDataModel(capabilities))
    {
        GTEST_SKIP() << "Schema/type rejection is covered by tableLifecycleIsIdempotent";
    }

    createTable<models::SomeDataModel>();

    if (not capabilities.mutations.insert)
    {
        expectDatabaseError([this]() { database.insert(models::SomeDataModel{}); },
                            orm::DatabaseErrorCode::UnsupportedFeature, GetParam().type, "insert");
        return;
    }

    database.insert(std::vector<models::SomeDataModel>{{1, "one", 1.5}, {2, "two", 2.5}});

    orm::Query<models::SomeDataModel> query;
    query.orderBy(asc(col<&models::SomeDataModel::field1>()));
    const auto returnedModels = database.select(query);

    ASSERT_EQ(returnedModels.size(), 2);
    EXPECT_EQ(returnedModels[0].field1, 1);
    EXPECT_EQ(returnedModels[0].field2, "one");
    EXPECT_DOUBLE_EQ(returnedModels[0].field3, 1.5);
    EXPECT_EQ(returnedModels[1].field1, 2);
    EXPECT_EQ(returnedModels[1].field2, "two");
    EXPECT_DOUBLE_EQ(returnedModels[1].field3, 2.5);
}

TEST_P(BackendConformanceTest, advertisedScalarTypesRoundTripAtDeclaredBoundaries)
{
    const auto& capabilities = connection.getBackendCapabilities();
    if (not canPopulateModelWithId(capabilities) or not supportsAllBasicTypes(capabilities))
    {
        GTEST_SKIP() << "The backend does not advertise the complete built-in scalar profile";
    }

    createTable<models::ModelWithAllBasicTypes>();

    const auto unsigned64Maximum =
        capabilities.valueLimits.maxUnsignedLongLong.value_or(std::numeric_limits<unsigned long long>::max());
    const auto unsignedLongMaximum =
        std::min(static_cast<unsigned long long>(std::numeric_limits<unsigned long>::max()), unsigned64Maximum);
    const auto rows = std::vector<models::ModelWithAllBasicTypes>{
        {1, false, std::numeric_limits<char>::lowest(), std::numeric_limits<unsigned char>::lowest(),
         std::numeric_limits<short>::lowest(), std::numeric_limits<unsigned short>::lowest(),
         std::numeric_limits<int>::lowest(), std::numeric_limits<unsigned int>::lowest(),
         std::numeric_limits<long>::lowest(), std::numeric_limits<unsigned long>::lowest(),
         std::numeric_limits<long long>::lowest(), 0, -123.5F, -9876.25, "lowest"},
        {2, true, std::numeric_limits<char>::max(), std::numeric_limits<unsigned char>::max(),
         std::numeric_limits<short>::max(), std::numeric_limits<unsigned short>::max(), std::numeric_limits<int>::max(),
         std::numeric_limits<unsigned int>::max(), std::numeric_limits<long>::max(),
         static_cast<unsigned long>(unsignedLongMaximum), std::numeric_limits<long long>::max(), unsigned64Maximum,
         123.5F, 9876.25, "highest"},
    };
    database.insert(rows);

    orm::Query<models::ModelWithAllBasicTypes> query;
    query.orderBy(asc(col<&models::ModelWithAllBasicTypes::id>()));
    const auto returnedRows = database.select(query);

    ASSERT_EQ(returnedRows.size(), rows.size());
    EXPECT_EQ(returnedRows[0].field1, rows[0].field1);
    EXPECT_EQ(returnedRows[0].field2, rows[0].field2);
    EXPECT_EQ(returnedRows[0].field3, rows[0].field3);
    EXPECT_EQ(returnedRows[0].field4, rows[0].field4);
    EXPECT_EQ(returnedRows[0].field5, rows[0].field5);
    EXPECT_EQ(returnedRows[0].field6, rows[0].field6);
    EXPECT_EQ(returnedRows[0].field7, rows[0].field7);
    EXPECT_EQ(returnedRows[0].field8, rows[0].field8);
    EXPECT_EQ(returnedRows[0].field9, rows[0].field9);
    EXPECT_EQ(returnedRows[0].field10, rows[0].field10);
    EXPECT_EQ(returnedRows[0].field11, rows[0].field11);
    EXPECT_FLOAT_EQ(returnedRows[0].field12, rows[0].field12);
    EXPECT_DOUBLE_EQ(returnedRows[0].field13, rows[0].field13);
    EXPECT_EQ(returnedRows[0].field14, rows[0].field14);
    EXPECT_EQ(returnedRows[1].field1, rows[1].field1);
    EXPECT_EQ(returnedRows[1].field2, rows[1].field2);
    EXPECT_EQ(returnedRows[1].field3, rows[1].field3);
    EXPECT_EQ(returnedRows[1].field4, rows[1].field4);
    EXPECT_EQ(returnedRows[1].field5, rows[1].field5);
    EXPECT_EQ(returnedRows[1].field6, rows[1].field6);
    EXPECT_EQ(returnedRows[1].field7, rows[1].field7);
    EXPECT_EQ(returnedRows[1].field8, rows[1].field8);
    EXPECT_EQ(returnedRows[1].field9, rows[1].field9);
    EXPECT_EQ(returnedRows[1].field10, rows[1].field10);
    EXPECT_EQ(returnedRows[1].field11, rows[1].field11);
    EXPECT_FLOAT_EQ(returnedRows[1].field12, rows[1].field12);
    EXPECT_DOUBLE_EQ(returnedRows[1].field13, rows[1].field13);
    EXPECT_EQ(returnedRows[1].field14, rows[1].field14);
}

TEST_P(BackendConformanceTest, preparedValuesPreserveInjectionLikeText)
{
    const auto& capabilities = connection.getBackendCapabilities();
    if (not canPopulateSomeDataModel(capabilities))
    {
        GTEST_SKIP() << "Schema/type/insert rejection is covered by other conformance tests";
    }

    createTable<models::SomeDataModel>();
    const auto injectedValue = std::string{"x' OR 1=1 --"};
    database.insert(std::vector<models::SomeDataModel>{{1, injectedValue, 1.0}, {2, "safe", 2.0}});

    orm::Query<models::SomeDataModel> query;
    query.where(col<&models::SomeDataModel::field2>() == injectedValue);
    const auto rows = database.select(query);

    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0].field1, 1);
    EXPECT_EQ(rows[0].field2, injectedValue);
}

TEST_P(BackendConformanceTest, mappedTableAndColumnNamesRoundTrip)
{
    const auto& capabilities = connection.getBackendCapabilities();

    if (not canPopulateModelWithId(capabilities))
    {
        GTEST_SKIP() << "Schema/type/insert rejection is covered by other conformance tests";
    }

    createTable<conformance_models::MappedModel>();
    database.insert(conformance_models::MappedModel{7, "mapped"});

    orm::Query<conformance_models::MappedModel> query;
    query.where(col<&conformance_models::MappedModel::id>() == 7);
    const auto rows = database.select(query);

    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0].id, 7);
    EXPECT_EQ(rows[0].name, "mapped");
}

TEST_P(BackendConformanceTest, createTableRejectsNonPortablePhysicalBindNames)
{
    const auto& capabilities = connection.getBackendCapabilities();

    if (not supportsModelWithId(capabilities) or not capabilities.schema.createTableIfNotExists)
    {
        GTEST_SKIP() << "Schema/type rejection is covered by tableLifecycleIsIdempotent";
    }

    expectDatabaseError([this]() { database.createTable<conformance_models::NonPortableBindNameModel>(); },
                        orm::DatabaseErrorCode::UnsupportedFeature, GetParam().type, "create table");
}

TEST_P(BackendConformanceTest, reservedTableAndColumnNamesRoundTrip)
{
    const auto& capabilities = connection.getBackendCapabilities();
    if (not canPopulateModelWithId(capabilities))
    {
        GTEST_SKIP() << "Schema/type/insert rejection is covered by other conformance tests";
    }

    createTable<conformance_models::ReservedIdentifierModel>();
    database.insert(conformance_models::ReservedIdentifierModel{1, "quoted"});

    orm::Query<conformance_models::ReservedIdentifierModel> query;
    query.where(col<&conformance_models::ReservedIdentifierModel::value>() == "quoted");
    const auto rows = database.select(query);

    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0].id, 1);
    EXPECT_EQ(rows[0].value, "quoted");
}

TEST_P(BackendConformanceTest, compositePrimaryKeysFollowAdvertisedCapability)
{
    const auto& capabilities = connection.getBackendCapabilities();

    if (supportsModelWithId(capabilities) and capabilities.schema.createTableIfNotExists and
        not capabilities.schema.compositePrimaryKeys)
    {
        expectDatabaseError([this]() { database.createTable<models::ModelWithOverwrittenId>(); },
                            orm::DatabaseErrorCode::UnsupportedFeature, GetParam().type, "create table");
        return;
    }

    if (not canPopulateModelWithId(capabilities) or not capabilities.schema.compositePrimaryKeys)
    {
        GTEST_SKIP() << "Schema/type/insert rejection is covered by other conformance tests";
    }

    createTable<models::ModelWithOverwrittenId>();
    database.insert(std::vector<models::ModelWithOverwrittenId>{{100, 1, "one"}, {200, 2, "two"}});

    orm::Query<models::ModelWithOverwrittenId> query;
    query.orderBy(asc(col<&models::ModelWithOverwrittenId::field1>()));
    const auto rows = database.select(query);

    ASSERT_EQ(rows.size(), 2);
    EXPECT_EQ(rows[0].id, 100);
    EXPECT_EQ(rows[0].field1, 1);
    EXPECT_EQ(rows[0].field2, "one");
    EXPECT_EQ(rows[1].id, 200);
    EXPECT_EQ(rows[1].field1, 2);
    EXPECT_EQ(rows[1].field2, "two");
}

TEST_P(BackendConformanceTest, generatedPrimaryKeysFollowAdvertisedCapability)
{
    const auto& capabilities = connection.getBackendCapabilities();

    if (supportsModelWithId(capabilities) and capabilities.schema.createTableIfNotExists and
        not capabilities.schema.autoIncrementPrimaryKey)
    {
        expectDatabaseError([this]() { database.createTable<models::ModelWithAutoIncrementId>(); },
                            orm::DatabaseErrorCode::UnsupportedFeature, GetParam().type, "create table");
        return;
    }

    if (not canPopulateModelWithId(capabilities) or not capabilities.schema.autoIncrementPrimaryKey)
    {
        GTEST_SKIP() << "Schema/type/insert rejection is covered by other conformance tests";
    }

    createTable<models::ModelWithAutoIncrementId>();
    database.insert(std::vector<models::ModelWithAutoIncrementId>{{0, 10, "first"}, {0, 20, "second"}});

    orm::Query<models::ModelWithAutoIncrementId> query;
    query.orderBy(asc(col<&models::ModelWithAutoIncrementId::id>()));
    const auto rows = database.select(query);

    ASSERT_EQ(rows.size(), 2);
    EXPECT_NE(rows[0].id, rows[1].id);

    orm::Query<models::ModelWithAutoIncrementId> selectedByGeneratedKey;
    selectedByGeneratedKey.where(col<&models::ModelWithAutoIncrementId::id>() == rows[0].id);
    const auto selectedRows = database.select(selectedByGeneratedKey);

    ASSERT_EQ(selectedRows.size(), 1);
    EXPECT_EQ(selectedRows[0].field1, rows[0].field1);
    EXPECT_EQ(selectedRows[0].field2, rows[0].field2);
}

TEST_P(BackendConformanceTest, nullableValuesRoundTripWithoutNullLoss)
{
    const auto& capabilities = connection.getBackendCapabilities();

    if (not canPopulateSomeDataModel(capabilities))
    {
        GTEST_SKIP() << "Schema/type/insert rejection is covered by other conformance tests";
    }

    createTable<models::ModelWithOptional>();
    database.insert(
        std::vector<models::ModelWithOptional>{{std::nullopt, std::nullopt, std::nullopt}, {42, "present", 3.5}});

    orm::Query<models::ModelWithOptional> nullQuery;
    nullQuery.where(col<&models::ModelWithOptional::field1>().isNull() and
                    col<&models::ModelWithOptional::field2>().isNull() and
                    col<&models::ModelWithOptional::field3>().isNull());
    const auto nullRows = database.select(nullQuery);

    ASSERT_EQ(nullRows.size(), 1);
    EXPECT_FALSE(nullRows[0].field1.has_value());
    EXPECT_FALSE(nullRows[0].field2.has_value());
    EXPECT_FALSE(nullRows[0].field3.has_value());

    orm::Query<models::ModelWithOptional> presentQuery;
    presentQuery.where(col<&models::ModelWithOptional::field1>() == 42);
    const auto presentRows = database.select(presentQuery);

    ASSERT_EQ(presentRows.size(), 1);
    EXPECT_EQ(presentRows[0].field1, std::optional<int>{42});
    EXPECT_EQ(presentRows[0].field2, std::optional<std::string>{"present"});
    EXPECT_EQ(presentRows[0].field3, std::optional<double>{3.5});
}

TEST_P(BackendConformanceTest, nullableToOneRelationRoundTripsWithoutNullLoss)
{
    const auto& capabilities = connection.getBackendCapabilities();
    if (not canPopulateModelWithId(capabilities) or not capabilities.schema.foreignKeys or
        not capabilities.relations.toOne)
    {
        GTEST_SKIP() << "Schema/type/to-one rejection is covered by other conformance tests";
    }

    createTable<models::ModelWithId>();
    createTable<models::ModelOptionallyRelatedToOtherModel>();
    database.insert(models::ModelOptionallyRelatedToOtherModel{1, 10, "without-target", std::nullopt});

    orm::Query<models::ModelOptionallyRelatedToOtherModel> query;
    query.where(col<&models::ModelOptionallyRelatedToOtherModel::id>() == 1);
    const auto rows = database.select(query);

    ASSERT_EQ(rows.size(), 1);
    EXPECT_FALSE(rows[0].field3.has_value());
}

TEST_P(BackendConformanceTest, projectionFollowsAdvertisedCapability)
{
    const auto& capabilities = connection.getBackendCapabilities();
    orm::ProjectionQuery<models::ModelWithId, conformance_models::Projection> query;
    query.project(as("id", col<&models::ModelWithId::id>()), as("name", col<&models::ModelWithId::field2>()));

    if (supportsModelWithId(capabilities) and not capabilities.query.projections)
    {
        expectDatabaseError([this, &query]() { (void)database.select(query); },
                            orm::DatabaseErrorCode::UnsupportedFeature, GetParam().type, "select projection");
        return;
    }

    if (not canPopulateModelWithId(capabilities))
    {
        GTEST_SKIP() << "Schema/type/insert rejection is covered by other conformance tests";
    }

    createTable<models::ModelWithId>();
    database.insert(models::ModelWithId{7, 70, "projected"});

    const auto rows = database.select(query);

    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0].id, 7);
    EXPECT_EQ(rows[0].name, "projected");
}

TEST_P(BackendConformanceTest, lossyProjectionHydrationReportsConversionError)
{
    const auto& capabilities = connection.getBackendCapabilities();

    if (not capabilities.query.projections)
    {
        GTEST_SKIP() << "Projection rejection is covered by projectionFollowsAdvertisedCapability";
    }

    if (not canPopulateModelWithId(capabilities))
    {
        GTEST_SKIP() << "Schema/type/insert rejection is covered by other conformance tests";
    }

    createTable<models::ModelWithId>();
    database.insert(models::ModelWithId{1, 300, "too-wide"});

    orm::ProjectionQuery<models::ModelWithId, conformance_models::NarrowProjection> query;
    query.project(as("value", col<&models::ModelWithId::field1>()));

    expectDatabaseError([this, &query]() { (void)database.select(query); }, orm::DatabaseErrorCode::Conversion,
                        GetParam().type, "select projection");
}

TEST_P(BackendConformanceTest, duplicatePrimaryKeyReportsConstraintError)
{
    const auto& capabilities = connection.getBackendCapabilities();

    if (not canPopulateModelWithId(capabilities))
    {
        GTEST_SKIP() << "Schema/type/insert rejection is covered by other conformance tests";
    }

    createTable<models::ModelWithId>();
    database.insert(models::ModelWithId{1, 10, "first"});

    expectDatabaseError([this]() { database.insert(models::ModelWithId{1, 20, "duplicate"}); },
                        orm::DatabaseErrorCode::Constraint, GetParam().type, "insert");

    orm::Query<models::ModelWithId> query;
    const auto rows = database.select(query);
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0].field2, "first");
}

TEST_P(BackendConformanceTest, toOneRelationFollowsAdvertisedCapability)
{
    const auto& capabilities = connection.getBackendCapabilities();

    if (supportsModelWithId(capabilities) and (not capabilities.schema.foreignKeys or not capabilities.relations.toOne))
    {
        expectDatabaseError([this]() { database.createTable<models::ModelRelatedToOtherModel>(); },
                            orm::DatabaseErrorCode::UnsupportedFeature, GetParam().type, "create table");
        return;
    }

    if (not canPopulateModelWithId(capabilities) or not capabilities.schema.foreignKeys or
        not capabilities.relations.toOne)
    {
        GTEST_SKIP() << "Schema/type/insert rejection is covered by other conformance tests";
    }

    createTable<models::ModelWithId>();
    createTable<models::ModelRelatedToOtherModel>();
    const auto target = models::ModelWithId{7, 70, "target"};
    database.insert(target);
    database.insert(models::ModelRelatedToOtherModel{1, 10, "owner", target});

    orm::Query<models::ModelRelatedToOtherModel> query;
    query.where(col<&models::ModelRelatedToOtherModel::id>() == 1);
    const auto rows = database.select(query);

    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0].field3.id, target.id);
    EXPECT_EQ(rows[0].field3.field1, target.field1);
    EXPECT_EQ(rows[0].field3.field2, target.field2);
}

TEST_P(BackendConformanceTest, toOneForeignKeyRejectsAMissingEndpoint)
{
    const auto& capabilities = connection.getBackendCapabilities();

    if (not canPopulateModelWithId(capabilities) or not capabilities.schema.foreignKeys or
        not capabilities.relations.toOne)
    {
        GTEST_SKIP() << "Schema/type/to-one rejection is covered by other conformance tests";
    }

    createTable<models::ModelWithId>();
    createTable<models::ModelRelatedToOtherModel>();
    const auto missingTarget = models::ModelWithId{999, 0, "missing"};

    expectDatabaseError([this, &missingTarget]()
                        { database.insert(models::ModelRelatedToOtherModel{1, 10, "owner", missingTarget}); },
                        orm::DatabaseErrorCode::Constraint, GetParam().type, "insert");
}

TEST_P(BackendConformanceTest, oneToManyPredicateAndUnlinkFollowAdvertisedCapabilities)
{
    const auto& capabilities = connection.getBackendCapabilities();
    const auto author = collection_models::Author{1, "author", {}};
    const auto book = collection_models::Book{10, "portable-book", std::nullopt};

    if (not capabilities.relations.oneToMany)
    {
        expectDatabaseError([this, &author, &book]()
                            { (void)database.link<&collection_models::Author::books>(author, book); },
                            orm::DatabaseErrorCode::UnsupportedFeature, GetParam().type, "link relation");
        return;
    }

    if (not capabilities.mutations.update)
    {
        expectDatabaseError([this, &author, &book]()
                            { (void)database.link<&collection_models::Author::books>(author, book); },
                            orm::DatabaseErrorCode::UnsupportedFeature, GetParam().type, "link relation");
        return;
    }

    if (not canPopulateModelWithId(capabilities) or not capabilities.schema.foreignKeys or
        not capabilities.relations.toOne)
    {
        GTEST_SKIP() << "Schema/type/to-one rejection is covered by other conformance tests";
    }

    createTable<collection_models::Author>();
    createTable<collection_models::Book>();
    database.insert(author);
    database.insert(book);

    if (capabilities.mutations.affectedRows != orm::db::AffectedRowsSupport::Reliable)
    {
        expectDatabaseError([this, &author, &book]()
                            { (void)database.link<&collection_models::Author::books>(author, book); },
                            orm::DatabaseErrorCode::AffectedRowsUnavailable, GetParam().type, "link relation");
        return;
    }

    ASSERT_EQ(database.link<&collection_models::Author::books>(author, book), 1);

    orm::Query<collection_models::Author> query;
    query.where(any<&collection_models::Author::books>(col<&collection_models::Book::title>() == book.title));

    if (not capabilities.query.collectionPredicates or not capabilities.relations.collectionPredicates)
    {
        expectDatabaseError([this, &query]() { (void)database.select(query); },
                            orm::DatabaseErrorCode::UnsupportedFeature, GetParam().type, "select");
        EXPECT_EQ(database.unlink<&collection_models::Author::books>(author, book), 1);
        return;
    }

    const auto linkedAuthors = database.select(query);
    ASSERT_EQ(linkedAuthors.size(), 1);
    EXPECT_EQ(linkedAuthors[0].id, author.id);
    EXPECT_FALSE(linkedAuthors[0].books.isLoaded());

    EXPECT_EQ(database.unlink<&collection_models::Author::books>(author, book), 1);
    EXPECT_TRUE(database.select(query).empty());
}

TEST_P(BackendConformanceTest, collectionRelationFollowsAdvertisedCapabilities)
{
    const auto& capabilities = connection.getBackendCapabilities();

    if (not canPopulateModelWithId(capabilities))
    {
        GTEST_SKIP() << "Schema/type/insert rejection is covered by other conformance tests";
    }

    createTable<collection_models::User>();
    createTable<collection_models::Role>();

    const auto relationSchemaSupported = capabilities.schema.createTableIfNotExists and
                                         capabilities.schema.compositePrimaryKeys and
                                         capabilities.schema.foreignKeys and capabilities.schema.onDeleteCascade and
                                         capabilities.relations.junctionTables and capabilities.relations.manyToMany;

    if (not relationSchemaSupported)
    {
        expectDatabaseError([this]() { database.createRelationTables<collection_models::User>(); },
                            orm::DatabaseErrorCode::UnsupportedFeature, GetParam().type, "create relation tables");
        return;
    }

    createRelationTables<collection_models::User>();
    const auto user = collection_models::User{1, "user", {}};
    const auto role = collection_models::Role{2, "role", {}};
    database.insert(user);
    database.insert(role);

    if (not capabilities.mutations.atomicInsertIfAbsent)
    {
        expectDatabaseError([this, &user, &role]()
                            { (void)database.link<&collection_models::User::roles>(user, role); },
                            orm::DatabaseErrorCode::UnsupportedFeature, GetParam().type, "link relation");
        return;
    }

    if (capabilities.mutations.affectedRows != orm::db::AffectedRowsSupport::Reliable)
    {
        expectDatabaseError([this, &user, &role]()
                            { (void)database.link<&collection_models::User::roles>(user, role); },
                            orm::DatabaseErrorCode::AffectedRowsUnavailable, GetParam().type, "link relation");
        return;
    }

    EXPECT_EQ(database.link<&collection_models::User::roles>(user, role), 1);
    EXPECT_EQ(database.link<&collection_models::User::roles>(user, role), 0);
    orm::Query<collection_models::User> query;
    query.include<&collection_models::User::roles>();

    if (not capabilities.relations.collectionIncludes)
    {
        expectDatabaseError([this, &query]() { (void)database.select(query); },
                            orm::DatabaseErrorCode::UnsupportedFeature, GetParam().type, "include collection");
        return;
    }

    const auto rows = database.select(query);

    ASSERT_EQ(rows.size(), 1);
    ASSERT_TRUE(rows[0].roles.isLoaded());
    ASSERT_EQ(rows[0].roles.size(), 1);
    EXPECT_EQ(rows[0].roles[0].id, role.id);
}

TEST_P(BackendConformanceTest, compositeRelationKeysFollowAdvertisedCapability)
{
    const auto& capabilities = connection.getBackendCapabilities();

    if (not canPopulateModelWithId(capabilities) or not capabilities.schema.compositePrimaryKeys)
    {
        GTEST_SKIP() << "Composite schema/type/insert rejection is covered by other conformance tests";
    }

    createTable<collection_models::CompositeOwner>();
    createTable<collection_models::CompositeTag>();

    const auto genericRelationSchemaSupported =
        capabilities.schema.foreignKeys and capabilities.schema.onDeleteCascade and
        capabilities.relations.junctionTables and capabilities.relations.manyToMany;

    if (not genericRelationSchemaSupported)
    {
        GTEST_SKIP()
            << "Generic junction-table rejection is covered by collectionRelationFollowsAdvertisedCapabilities";
    }

    if (not capabilities.relations.compositeEndpointKeys)
    {
        expectDatabaseError([this]() { database.createRelationTables<collection_models::CompositeOwner>(); },
                            orm::DatabaseErrorCode::UnsupportedFeature, GetParam().type, "create relation tables");
        return;
    }

    createRelationTables<collection_models::CompositeOwner>();
    const auto owner = collection_models::CompositeOwner{"tenant-a", 1, "owner", {}};
    const auto tag = collection_models::CompositeTag{"scope-a", 10, "tag"};
    database.insert(owner);
    database.insert(tag);

    if (not capabilities.mutations.atomicInsertIfAbsent)
    {
        expectDatabaseError([this, &owner, &tag]()
                            { (void)database.link<&collection_models::CompositeOwner::tags>(owner, tag); },
                            orm::DatabaseErrorCode::UnsupportedFeature, GetParam().type, "link relation");
        return;
    }

    if (capabilities.mutations.affectedRows != orm::db::AffectedRowsSupport::Reliable)
    {
        expectDatabaseError([this, &owner, &tag]()
                            { (void)database.link<&collection_models::CompositeOwner::tags>(owner, tag); },
                            orm::DatabaseErrorCode::AffectedRowsUnavailable, GetParam().type, "link relation");
        return;
    }

    ASSERT_EQ(database.link<&collection_models::CompositeOwner::tags>(owner, tag), 1);

    if (capabilities.relations.collectionIncludes)
    {
        orm::Query<collection_models::CompositeOwner> query;
        query
            .where(col<&collection_models::CompositeOwner::tenant>() == owner.tenant and
                   col<&collection_models::CompositeOwner::id>() == owner.id)
            .include<&collection_models::CompositeOwner::tags>();
        const auto owners = database.select(query);

        ASSERT_EQ(owners.size(), 1);
        ASSERT_TRUE(owners[0].tags.isLoaded());
        ASSERT_EQ(owners[0].tags.size(), 1);
        EXPECT_EQ(owners[0].tags[0].scope, tag.scope);
        EXPECT_EQ(owners[0].tags[0].id, tag.id);
    }

    if (not capabilities.mutations.remove)
    {
        expectDatabaseError([this, &owner, &tag]()
                            { (void)database.unlink<&collection_models::CompositeOwner::tags>(owner, tag); },
                            orm::DatabaseErrorCode::UnsupportedFeature, GetParam().type, "unlink relation");
        return;
    }

    EXPECT_EQ(database.unlink<&collection_models::CompositeOwner::tags>(owner, tag), 1);
    EXPECT_EQ(database.unlink<&collection_models::CompositeOwner::tags>(owner, tag), 0);
}

TEST_P(BackendConformanceTest, deletingManyToManyEndpointCascadesOnlyTheJunctionLink)
{
    const auto& capabilities = connection.getBackendCapabilities();
    const auto relationSchemaSupported = capabilities.schema.compositePrimaryKeys and
                                         capabilities.schema.foreignKeys and capabilities.schema.onDeleteCascade and
                                         capabilities.relations.junctionTables and capabilities.relations.manyToMany;

    if (not canPopulateModelWithId(capabilities) or not relationSchemaSupported or
        not capabilities.relations.collectionIncludes or not capabilities.mutations.atomicInsertIfAbsent or
        not capabilities.mutations.remove or
        capabilities.mutations.affectedRows != orm::db::AffectedRowsSupport::Reliable)
    {
        GTEST_SKIP()
            << "Required schema, relation, mutation, or include behavior is covered by other conformance tests";
    }

    createTable<collection_models::User>();
    createTable<collection_models::Role>();
    createRelationTables<collection_models::User>();
    const auto user = collection_models::User{1, "user", {}};
    const auto role = collection_models::Role{2, "role", {}};
    database.insert(user);
    database.insert(role);
    ASSERT_EQ(database.link<&collection_models::User::roles>(user, role), 1);

    ASSERT_EQ(database.remove<collection_models::User>(col<&collection_models::User::id>() == user.id), 1);

    orm::Query<collection_models::Role> query;
    query.where(col<&collection_models::Role::id>() == role.id).include<&collection_models::Role::users>();
    const auto roles = database.select(query);

    ASSERT_EQ(roles.size(), 1);
    EXPECT_EQ(roles[0].id, role.id);
    EXPECT_TRUE(roles[0].users.isLoaded());
    EXPECT_TRUE(roles[0].users.empty());
}

TEST_P(BackendConformanceTest, relationMutationRollsBackWithExplicitTransaction)
{
    const auto& capabilities = connection.getBackendCapabilities();
    const auto relationSchemaSupported = capabilities.schema.compositePrimaryKeys and
                                         capabilities.schema.foreignKeys and capabilities.schema.onDeleteCascade and
                                         capabilities.relations.junctionTables and capabilities.relations.manyToMany;

    if (not canPopulateModelWithId(capabilities) or not relationSchemaSupported or
        not capabilities.relations.collectionIncludes or not capabilities.mutations.atomicInsertIfAbsent or
        capabilities.mutations.affectedRows != orm::db::AffectedRowsSupport::Reliable or not capabilities.transactions)
    {
        GTEST_SKIP() << "Required schema, relation, mutation, include, or transaction behavior is covered elsewhere";
    }

    createTable<collection_models::User>();
    createTable<collection_models::Role>();
    createRelationTables<collection_models::User>();
    const auto user = collection_models::User{1, "user", {}};
    const auto role = collection_models::Role{2, "role", {}};
    database.insert(user);
    database.insert(role);

    connection.beginTransaction();
    ASSERT_EQ(database.link<&collection_models::User::roles>(user, role), 1);
    connection.rollbackTransaction();

    orm::Query<collection_models::User> query;
    query.where(col<&collection_models::User::id>() == user.id).include<&collection_models::User::roles>();
    const auto users = database.select(query);

    ASSERT_EQ(users.size(), 1);
    EXPECT_TRUE(users[0].roles.isLoaded());
    EXPECT_TRUE(users[0].roles.empty());
}

TEST_P(BackendConformanceTest, explicitTransactionRollbackRestoresPreviousState)
{
    const auto& capabilities = connection.getBackendCapabilities();

    if (not capabilities.transactions)
    {
        expectDatabaseError([this]() { connection.beginTransaction(); }, orm::DatabaseErrorCode::UnsupportedFeature,
                            GetParam().type, "begin transaction");
        return;
    }

    if (not canPopulateSomeDataModel(capabilities))
    {
        connection.beginTransaction();
        EXPECT_NO_THROW(connection.rollbackTransaction());
        return;
    }

    createTable<models::SomeDataModel>();

    connection.beginTransaction();
    database.insert(models::SomeDataModel{1, "rolled-back", 1.0});
    connection.rollbackTransaction();

    orm::Query<models::SomeDataModel> query;
    EXPECT_TRUE(database.select(query).empty());
}

TEST_P(BackendConformanceTest, explicitTransactionCommitPersistsChanges)
{
    const auto& capabilities = connection.getBackendCapabilities();

    if (not capabilities.transactions)
    {
        expectDatabaseError([this]() { connection.beginTransaction(); }, orm::DatabaseErrorCode::UnsupportedFeature,
                            GetParam().type, "begin transaction");
        return;
    }

    if (not canPopulateSomeDataModel(capabilities))
    {
        GTEST_SKIP() << "Schema/type/insert rejection is covered by other conformance tests";
    }

    createTable<models::SomeDataModel>();
    connection.beginTransaction();
    database.insert(models::SomeDataModel{1, "committed", 1.0});
    connection.commitTransaction();

    orm::Query<models::SomeDataModel> query;
    query.where(col<&models::SomeDataModel::field1>() == 1);
    const auto rows = database.select(query);

    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0].field2, "committed");
}

TEST_P(BackendConformanceTest, disconnectRollsBackActiveTransactionAndAllowsReconnect)
{
    const auto& capabilities = connection.getBackendCapabilities();

    if (not capabilities.transactions or not canPopulateSomeDataModel(capabilities))
    {
        GTEST_SKIP() << "Transaction/schema/type/insert rejection is covered by other conformance tests";
    }

    createTable<models::SomeDataModel>();
    connection.beginTransaction();
    database.insert(models::SomeDataModel{1, "uncommitted", 1.0});

    ASSERT_NO_THROW(connection.disconnect());
    EXPECT_FALSE(connection.isConnected());
    ASSERT_NO_THROW(connection.connect(GetParam().type, testConnectionString()));

    // Recreate SQLite's in-memory table while remaining a no-op for persistent
    // backends. In both cases the uncommitted row must be absent.
    ASSERT_NO_THROW(database.createTable<models::SomeDataModel>());
    orm::Query<models::SomeDataModel> query;
    EXPECT_TRUE(database.select(query).empty());

    ASSERT_NO_THROW(connection.beginTransaction());
    EXPECT_NO_THROW(connection.rollbackTransaction());
}

TEST_P(BackendConformanceTest, rollbackRecoversConnectionAfterConstraintError)
{
    const auto& capabilities = connection.getBackendCapabilities();
    if (not capabilities.transactions or not canPopulateModelWithId(capabilities))
    {
        GTEST_SKIP() << "Transaction/schema/type rejection is covered by other conformance tests";
    }

    createTable<models::ModelWithId>();
    database.insert(models::ModelWithId{1, 10, "existing"});
    connection.beginTransaction();

    expectDatabaseError([this]() { database.insert(models::ModelWithId{1, 20, "duplicate"}); },
                        orm::DatabaseErrorCode::Constraint, GetParam().type, "insert");
    EXPECT_NO_THROW(connection.rollbackTransaction());
    EXPECT_NO_THROW(database.insert(models::ModelWithId{2, 20, "recovered"}));

    orm::Query<models::ModelWithId> query;
    query.orderBy(asc(col<&models::ModelWithId::id>()));
    const auto rows = database.select(query);
    ASSERT_EQ(rows.size(), 2);
    EXPECT_EQ(rows[0].field2, "existing");
    EXPECT_EQ(rows[1].field2, "recovered");
}

TEST_P(BackendConformanceTest, orderedLimitFollowsAdvertisedCapability)
{
    const auto& capabilities = connection.getBackendCapabilities();
    orm::Query<models::SomeDataModel> query;
    query.orderBy(asc(col<&models::SomeDataModel::field1>())).limit(2);

    if (supportsSomeDataModel(capabilities) and not capabilities.query.limit)
    {
        expectDatabaseError([this, &query]() { (void)database.select(query); },
                            orm::DatabaseErrorCode::UnsupportedFeature, GetParam().type, "select");
        return;
    }

    if (not canPopulateSomeDataModel(capabilities))
    {
        GTEST_SKIP() << "Schema/type/insert rejection is covered by other conformance tests";
    }

    createTable<models::SomeDataModel>();
    database.insert(std::vector<models::SomeDataModel>{{1, "one", 1.0}, {2, "two", 2.0}, {3, "three", 3.0}});

    const auto rows = database.select(query);

    ASSERT_EQ(rows.size(), 2);
    EXPECT_EQ(rows[0].field1, 1);
    EXPECT_EQ(rows[1].field1, 2);
}

TEST_P(BackendConformanceTest, groupByAndHavingFollowAdvertisedCapabilities)
{
    const auto& capabilities = connection.getBackendCapabilities();
    orm::Query<models::ModelWithId> query;
    query.groupBy(col<&models::ModelWithId::field2>())
        .having(countAll<models::ModelWithId>() == 2)
        .orderBy(asc(col<&models::ModelWithId::field2>()));

    if (supportsModelWithId(capabilities) and
        (not capabilities.query.groupBy or not capabilities.query.having or not capabilities.query.fullModelGrouping))
    {
        expectDatabaseError([this, &query]() { (void)database.select(query); },
                            orm::DatabaseErrorCode::UnsupportedFeature, GetParam().type, "select");
        return;
    }

    if (not canPopulateModelWithId(capabilities))
    {
        GTEST_SKIP() << "Schema/type/insert rejection is covered by other conformance tests";
    }

    createTable<models::ModelWithId>();
    database.insert(std::vector<models::ModelWithId>{
        {1, 10, "alpha"}, {2, 20, "alpha"}, {3, 30, "beta"}, {4, 40, "beta"}, {5, 50, "single"}});

    const auto rows = database.select(query);

    ASSERT_EQ(rows.size(), 2);
    EXPECT_EQ(rows[0].field2, "alpha");
    EXPECT_EQ(rows[1].field2, "beta");
}

TEST_P(BackendConformanceTest, groupedProjectionWorksIndependentlyOfFullModelGrouping)
{
    const auto& capabilities = connection.getBackendCapabilities();
    orm::ProjectionQuery<models::ModelWithId, conformance_models::GroupedProjection> query;
    query.project(as("name", col<&models::ModelWithId::field2>()), as("users", countAll<models::ModelWithId>()))
        .groupBy(col<&models::ModelWithId::field2>())
        .having(countAll<models::ModelWithId>() >= 2)
        .orderBy(asc(col<&models::ModelWithId::field2>()));

    if (supportsModelWithId(capabilities) and
        (not capabilities.query.projections or not capabilities.query.groupBy or not capabilities.query.having))
    {
        expectDatabaseError([this, &query]() { (void)database.select(query); },
                            orm::DatabaseErrorCode::UnsupportedFeature, GetParam().type, "select projection");
        return;
    }

    if (not canPopulateModelWithId(capabilities))
    {
        GTEST_SKIP() << "Schema/type/insert rejection is covered by other conformance tests";
    }

    createTable<models::ModelWithId>();
    database.insert(std::vector<models::ModelWithId>{
        {1, 10, "alpha"}, {2, 20, "alpha"}, {3, 30, "beta"}, {4, 40, "beta"}, {5, 50, "single"}});

    const auto rows = database.select(query);

    ASSERT_EQ(rows.size(), 2);
    EXPECT_EQ(rows[0].name, "alpha");
    EXPECT_EQ(rows[0].users, 2);
    EXPECT_EQ(rows[1].name, "beta");
    EXPECT_EQ(rows[1].users, 2);
}

TEST_P(BackendConformanceTest, nullableAverageProjectionReturnsNullForAnEmptyResult)
{
    const auto& capabilities = connection.getBackendCapabilities();

    if (not capabilities.query.projections)
    {
        GTEST_SKIP() << "Projection rejection is covered by projectionFollowsAdvertisedCapability";
    }

    if (not canCreateAndDropSomeDataModel(capabilities))
    {
        GTEST_SKIP() << "Schema/type rejection is covered by other conformance tests";
    }

    createTable<models::ModelWithOptional>();
    orm::ProjectionQuery<models::ModelWithOptional, conformance_models::NullableAggregateProjection> query;
    query.project(as("averageValue", avg(col<&models::ModelWithOptional::field3>())));
    const auto rows = database.select(query);

    ASSERT_EQ(rows.size(), 1);
    EXPECT_FALSE(rows[0].averageValue.has_value());
}

TEST_P(BackendConformanceTest, orderedOffsetWithoutLimitReturnsRemainingRows)
{
    const auto& capabilities = connection.getBackendCapabilities();
    orm::Query<models::SomeDataModel> query;
    query.orderBy(asc(col<&models::SomeDataModel::field1>())).offset(1);

    if (supportsSomeDataModel(capabilities) and
        (not capabilities.query.offset or not capabilities.query.offsetWithoutLimit))
    {
        expectDatabaseError([this, &query]() { (void)database.select(query); },
                            orm::DatabaseErrorCode::UnsupportedFeature, GetParam().type, "select");
        return;
    }

    if (not canPopulateSomeDataModel(capabilities))
    {
        GTEST_SKIP() << "Schema/type/insert rejection is covered by other conformance tests";
    }

    createTable<models::SomeDataModel>();
    database.insert(std::vector<models::SomeDataModel>{{1, "one", 1.0}, {2, "two", 2.0}, {3, "three", 3.0}});

    const auto returnedModels = database.select(query);

    ASSERT_EQ(returnedModels.size(), 2);
    EXPECT_EQ(returnedModels[0].field1, 2);
    EXPECT_EQ(returnedModels[1].field1, 3);
}

TEST_P(BackendConformanceTest, mutationsReportExactAffectedRows)
{
    const auto& capabilities = connection.getBackendCapabilities();

    if (not canPopulateSomeDataModel(capabilities))
    {
        GTEST_SKIP() << "Schema/type/insert rejection is covered by other conformance tests";
    }

    createTable<models::SomeDataModel>();
    database.insert(std::vector<models::SomeDataModel>{{1, "one", 1.0}, {2, "two", 2.0}, {3, "three", 3.0}});

    orm::Update<models::SomeDataModel> update;
    update.set(col<&models::SomeDataModel::field2>(), "updated").where(col<&models::SomeDataModel::field1>() >= 2);

    if (not capabilities.mutations.update)
    {
        expectDatabaseError([this, &update]() { (void)database.update(update); },
                            orm::DatabaseErrorCode::UnsupportedFeature, GetParam().type, "update");
    }
    else if (capabilities.mutations.affectedRows != orm::db::AffectedRowsSupport::Reliable)
    {
        expectDatabaseError([this, &update]() { (void)database.update(update); },
                            orm::DatabaseErrorCode::AffectedRowsUnavailable, GetParam().type, "update");
    }
    else
    {
        EXPECT_EQ(database.update(update), 2);
    }

    if (not capabilities.mutations.remove)
    {
        expectDatabaseError(
            [this]() { (void)database.remove<models::SomeDataModel>(col<&models::SomeDataModel::field1>() == 1); },
            orm::DatabaseErrorCode::UnsupportedFeature, GetParam().type, "remove");
    }
    else if (capabilities.mutations.affectedRows != orm::db::AffectedRowsSupport::Reliable)
    {
        expectDatabaseError(
            [this]() { (void)database.remove<models::SomeDataModel>(col<&models::SomeDataModel::field1>() == 1); },
            orm::DatabaseErrorCode::AffectedRowsUnavailable, GetParam().type, "remove");
    }
    else
    {
        EXPECT_EQ(database.remove<models::SomeDataModel>(col<&models::SomeDataModel::field1>() == 1), 1);
    }
}

TEST_P(BackendConformanceTest, typedFieldsCoverRenamedColumnsRelationsAggregatesAndNullableWrites)
{
    if (not GetParam().supported)
    {
        GTEST_SKIP() << "The complete typed query contract applies to supported backends";
    }

    using Account = conformance_models::TypedAccount;
    using Profile = conformance_models::MappedModel;
    using Role = collection_models::Role;

    createTable<Profile>();
    createTable<Role>();
    createTable<Account>();
    createRelationTables<Account>();
    const Profile firstProfile{10, "alpha"};
    const Profile secondProfile{20, "beta"};
    const Role admin{100, "admin", {}};
    const Role reader{200, "reader", {}};
    const std::vector<Account> accounts{{1, 18, "first@example.test", firstProfile, {}},
                                        {2, 25, std::nullopt, secondProfile, {}},
                                        {3, std::nullopt, "third@example.test", std::nullopt, {}}};
    database.insert(std::vector<Profile>{firstProfile, secondProfile});
    database.insert(std::vector<Role>{admin, reader});
    database.insert(accounts);
    ASSERT_EQ(database.link<&Account::roles>(accounts[0], admin), 1);
    ASSERT_EQ(database.link<&Account::roles>(accounts[1], reader), 1);

    orm::Query<Account> selected;
    selected
        .where((col<&Account::age>().between(short{18}, 30) && col<&Account::email>() != nullptr) ||
               col<&Account::profile, &Profile::id>().isNull())
        .orderBy(desc(col<&Account::id>()))
        .include<&Account::roles>();
    const auto rows = database.select(selected);
    ASSERT_EQ(rows.size(), 2);
    EXPECT_EQ(rows[0].id, 3);
    EXPECT_FALSE(rows[0].profile.has_value());
    EXPECT_TRUE(rows[0].roles.isLoaded());
    EXPECT_TRUE(rows[0].roles.empty());
    EXPECT_EQ(rows[1].id, 1);
    ASSERT_EQ(rows[1].roles.size(), 1);
    EXPECT_EQ(rows[1].roles[0].name, "admin");

    orm::Query<Account> rawQuery;
    rawQuery.where(raw<Account>("account_age = :age", param("age", 25))).orderBy(rawOrder<Account>("id DESC"));
    const auto rawRows = database.select(rawQuery);
    ASSERT_EQ(rawRows.size(), 1);
    EXPECT_EQ(rawRows[0].id, 2);

    orm::ProjectionQuery<Account, conformance_models::TypedAccountStats> statistics;
    statistics
        .project(as("profile", col<&Account::profile, &Profile::name>()), as("accounts", countAll<Account>()),
                 as("totalAge", sum(col<&Account::age>())), as("averageAge", avg(col<&Account::age>())),
                 as("minimumAge", min(col<&Account::age>())), as("maximumAge", max(col<&Account::age>())))
        .where(col<&Account::age>().in({18, 25}))
        .groupBy(col<&Account::profile, &Profile::name>())
        .having(countAll<Account>() >= 1 && avg(col<&Account::age>()) >= 18.0)
        .andHaving(sum(col<&Account::age>()) > 0)
        .orderBy(asc(col<&Account::profile, &Profile::name>()));
    const auto grouped = database.select(statistics);
    ASSERT_EQ(grouped.size(), 2);
    EXPECT_EQ(grouped[0].profile, "alpha");
    EXPECT_EQ(grouped[0].accounts, 1);
    EXPECT_EQ(grouped[0].totalAge, 18);
    EXPECT_EQ(grouped[0].averageAge, 18.0);
    EXPECT_EQ(grouped[0].minimumAge, 18);
    EXPECT_EQ(grouped[0].maximumAge, 18);
    EXPECT_EQ(grouped[1].profile, "beta");
    EXPECT_EQ(grouped[1].totalAge, 25);

    orm::Update<Account> promoted;
    promoted.set(col<&Account::age>(), std::optional<short>{21})
        .set(col<&Account::email>(), "promoted@example.test")
        .set(col<&Account::profile, &Profile::id>(), secondProfile.id)
        .where(any<&Account::roles>(col<&Role::name>() == "admin"));
    EXPECT_EQ(database.update(promoted), 1);

    orm::Query<Account> promotedQuery;
    promotedQuery.where(col<&Account::profile, &Profile::name>() == "beta").andWhere(col<&Account::age>() == 21);
    const auto promotedRows = database.select(promotedQuery);
    ASSERT_EQ(promotedRows.size(), 1);
    EXPECT_EQ(promotedRows[0].id, 1);
    EXPECT_EQ(promotedRows[0].email, "promoted@example.test");

    orm::Update<Account> detached;
    detached.set(col<&Account::profile, &Profile::id>(), std::nullopt)
        .set(col<&Account::age>(), std::optional<int>{})
        .where(col<&Account::id>() == 1);
    EXPECT_EQ(database.update(detached), 1);
    orm::Query<Account> missingValues;
    missingValues.where(col<&Account::profile, &Profile::id>() == nullptr && col<&Account::age>().isNull())
        .orderBy(asc(col<&Account::id>()));
    const auto nullRows = database.select(missingValues);
    ASSERT_EQ(nullRows.size(), 2);
    EXPECT_EQ(nullRows[0].id, 1);
    EXPECT_FALSE(nullRows[0].age.has_value());
    EXPECT_FALSE(nullRows[0].profile.has_value());
    EXPECT_EQ(nullRows[1].id, 3);

    EXPECT_EQ(database.remove<Account>(any<&Account::roles>(col<&Role::name>() == "admin")), 1);
    EXPECT_EQ(database.remove<Account>(none<&Account::roles>(col<&Role::name>() == "reader")), 1);
    orm::Query<Account> surviving;
    surviving.where(exists<&Account::roles>()).include<&Account::roles>();
    const auto survivors = database.select(surviving);
    ASSERT_EQ(survivors.size(), 1);
    EXPECT_EQ(survivors[0].id, 2);
    ASSERT_EQ(survivors[0].roles.size(), 1);
    EXPECT_EQ(survivors[0].roles[0].name, "reader");
}

TEST_P(BackendConformanceTest, collectionPredicatesKeepOuterOwnerWhenRelatedAliasMatchesItsTable)
{
    if (not GetParam().supported)
    {
        GTEST_SKIP() << "Collection alias correlation applies to supported backends";
    }

    using Owner = conformance_models::AliasOwner;
    using Target = conformance_models::AliasTarget;
    using Profile = conformance_models::AliasProfile;

    createTable<Profile>();
    createTable<Target>();
    createTable<Owner>();
    createRelationTables<Owner>();
    const Profile alpha{1, "alpha"};
    const Profile beta{2, "beta"};
    const Target matching{10, "match", alpha};
    const Target other{20, "other", beta};
    const std::vector<Owner> owners{{1, "first", {}}, {2, "second", {}}, {3, "empty", {}}};
    database.insert(std::vector<Profile>{alpha, beta});
    database.insert(std::vector<Target>{matching, other});
    database.insert(owners);
    ASSERT_EQ(database.link<&Owner::targets>(owners[0], matching), 1);
    ASSERT_EQ(database.link<&Owner::targets>(owners[1], other), 1);

    auto ownerIds = [this](const auto& predicate)
    {
        orm::Query<Owner> query;
        query.where(predicate).orderBy(asc(col<&Owner::id>()));
        std::vector<int> ids;
        for (const auto& owner : database.select(query))
            ids.push_back(owner.id);
        return ids;
    };
    EXPECT_EQ(ownerIds(any<&Owner::targets>(col<&Target::name>() == "match")), std::vector<int>{1});
    EXPECT_EQ((ownerIds(any<&Owner::targets>(col<&Target::profile, &Profile::name>() == "alpha"))),
              std::vector<int>{1});
    EXPECT_EQ(ownerIds(exists<&Owner::targets>()), (std::vector<int>{1, 2}));
    EXPECT_EQ(ownerIds(none<&Owner::targets>(col<&Target::name>() == "match")), (std::vector<int>{2, 3}));

    orm::Update<Owner> update;
    update.set(col<&Owner::name>(), "updated").where(any<&Owner::targets>(col<&Target::name>() == "match"));
    EXPECT_EQ(database.update(update), 1);
    EXPECT_EQ(ownerIds(col<&Owner::name>() == "updated"), std::vector<int>{1});
    EXPECT_EQ(ownerIds(col<&Owner::name>() == "second"), std::vector<int>{2});
    EXPECT_EQ(ownerIds(col<&Owner::name>() == "empty"), std::vector<int>{3});

    orm::Update<Owner> updateExisting;
    updateExisting.set(col<&Owner::name>(), "linked").where(exists<&Owner::targets>());
    EXPECT_EQ(database.update(updateExisting), 2);
    EXPECT_EQ(ownerIds(col<&Owner::name>() == "linked"), (std::vector<int>{1, 2}));
    orm::Update<Owner> updateMissing;
    updateMissing.set(col<&Owner::name>(), "unmatched").where(none<&Owner::targets>(col<&Target::name>() == "match"));
    EXPECT_EQ(database.update(updateMissing), 2);
    EXPECT_EQ(ownerIds(col<&Owner::name>() == "unmatched"), (std::vector<int>{2, 3}));

    EXPECT_EQ(database.remove<Owner>(any<&Owner::targets>(col<&Target::name>() == "match")), 1);
    EXPECT_EQ(ownerIds(col<&Owner::id>() > 0), (std::vector<int>{2, 3}));
    EXPECT_EQ(database.remove<Owner>(none<&Owner::targets>(col<&Target::name>() == "other")), 1);
    EXPECT_EQ(ownerIds(exists<&Owner::targets>()), std::vector<int>{2});
    EXPECT_EQ(database.remove<Owner>(exists<&Owner::targets>()), 1);
    EXPECT_TRUE(ownerIds(col<&Owner::id>() > 0).empty());
}

INSTANTIATE_TEST_SUITE_P(DatabaseTest, BackendConformanceTest, conformanceBackendTestConfigs, backendTestName);
