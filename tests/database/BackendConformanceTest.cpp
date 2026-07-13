#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "DatabaseTest.hpp"
#include "tests/CollectionModelsDefinitions.hpp"

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

struct MappedModel
{
    inline static constexpr std::string_view table_name = "conformance_mapped_table";
    inline static const std::map<std::string, std::string> columns_names = {
        {"id", "mapped_id"},
        {"name", "mapped_name"},
    };

    int id;
    std::string name;
};
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

class BackendConformanceTest : public DatabaseTest
{
};

TEST_P(BackendConformanceTest, connectionReportsSelectedBackendAndCoreCapabilities)
{
    ASSERT_TRUE(database.isConnected());
    EXPECT_EQ(database.getBackendType(), GetParam().type);

    const auto& capabilities = database.getBackendCapabilities();
    EXPECT_EQ(&capabilities, &database.getBackendCapabilities());
}

TEST_P(BackendConformanceTest, supportedBackendAdvertisesRequiredCoreCapabilities)
{
    if (not GetParam().supported)
    {
        GTEST_SKIP() << "Experimental backends may advertise a partial capability profile";
    }

    const auto& capabilities = database.getBackendCapabilities();

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
    orm::Database detectedDatabase;

    detectedDatabase.connect(GetParam().connectionString);

    EXPECT_TRUE(detectedDatabase.isConnected());
    EXPECT_EQ(detectedDatabase.getBackendType(), GetParam().type);
    detectedDatabase.disconnect();
}

TEST_P(BackendConformanceTest, connectedDatabaseRejectsSecondConnect)
{
    expectDatabaseError([this]() { database.connect(GetParam().connectionString); },
                        orm::DatabaseErrorCode::AlreadyConnected, GetParam().type, "connect");
    EXPECT_TRUE(database.isConnected());
}

TEST_P(BackendConformanceTest, disconnectedDatabaseRejectsCapabilitiesAndOperations)
{
    orm::Database disconnectedDatabase;

    expectDatabaseError([&disconnectedDatabase]() { (void)disconnectedDatabase.getBackendCapabilities(); },
                        orm::DatabaseErrorCode::NotConnected, orm::db::BackendType::Empty, "database operation");
    expectDatabaseError([&disconnectedDatabase]() { disconnectedDatabase.createTable<models::SomeDataModel>(); },
                        orm::DatabaseErrorCode::NotConnected, orm::db::BackendType::Empty, "database operation");
}

TEST_P(BackendConformanceTest, disconnectAllowsReconnect)
{
    database.disconnect();
    EXPECT_FALSE(database.isConnected());
    EXPECT_EQ(database.getBackendType(), orm::db::BackendType::Empty);

    database.connect(GetParam().type, GetParam().connectionString);

    EXPECT_TRUE(database.isConnected());
    EXPECT_EQ(database.getBackendType(), GetParam().type);
}

TEST_P(BackendConformanceTest, tableLifecycleIsIdempotent)
{
    const auto& capabilities = database.getBackendCapabilities();

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
    const auto& capabilities = database.getBackendCapabilities();

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
    query.orderBy(asc(col("field1")));
    const auto returnedModels = database.select(query);

    ASSERT_EQ(returnedModels.size(), 2);
    EXPECT_EQ(returnedModels[0].field1, 1);
    EXPECT_EQ(returnedModels[0].field2, "one");
    EXPECT_DOUBLE_EQ(returnedModels[0].field3, 1.5);
    EXPECT_EQ(returnedModels[1].field1, 2);
    EXPECT_EQ(returnedModels[1].field2, "two");
    EXPECT_DOUBLE_EQ(returnedModels[1].field3, 2.5);
}

TEST_P(BackendConformanceTest, mappedTableAndColumnNamesRoundTrip)
{
    const auto& capabilities = database.getBackendCapabilities();

    if (not canPopulateModelWithId(capabilities))
    {
        GTEST_SKIP() << "Schema/type/insert rejection is covered by other conformance tests";
    }

    createTable<conformance_models::MappedModel>();
    database.insert(conformance_models::MappedModel{7, "mapped"});

    orm::Query<conformance_models::MappedModel> query;
    query.where(col("id") == 7);
    const auto rows = database.select(query);

    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0].id, 7);
    EXPECT_EQ(rows[0].name, "mapped");
}

TEST_P(BackendConformanceTest, compositePrimaryKeysFollowAdvertisedCapability)
{
    const auto& capabilities = database.getBackendCapabilities();

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
    query.orderBy(asc(col("field1")));
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
    const auto& capabilities = database.getBackendCapabilities();

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
    query.orderBy(asc(col("id")));
    const auto rows = database.select(query);

    ASSERT_EQ(rows.size(), 2);
    EXPECT_NE(rows[0].id, rows[1].id);

    orm::Query<models::ModelWithAutoIncrementId> selectedByGeneratedKey;
    selectedByGeneratedKey.where(col("id") == rows[0].id);
    const auto selectedRows = database.select(selectedByGeneratedKey);

    ASSERT_EQ(selectedRows.size(), 1);
    EXPECT_EQ(selectedRows[0].field1, rows[0].field1);
    EXPECT_EQ(selectedRows[0].field2, rows[0].field2);
}

TEST_P(BackendConformanceTest, nullableValuesRoundTripWithoutNullLoss)
{
    const auto& capabilities = database.getBackendCapabilities();

    if (not canPopulateSomeDataModel(capabilities))
    {
        GTEST_SKIP() << "Schema/type/insert rejection is covered by other conformance tests";
    }

    createTable<models::ModelWithOptional>();
    database.insert(
        std::vector<models::ModelWithOptional>{{std::nullopt, std::nullopt, std::nullopt}, {42, "present", 3.5}});

    orm::Query<models::ModelWithOptional> nullQuery;
    nullQuery.where(col("field1").isNull() and col("field2").isNull() and col("field3").isNull());
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
    EXPECT_EQ(presentRows[0].field2, std::optional<std::string>{"present"});
    EXPECT_EQ(presentRows[0].field3, std::optional<double>{3.5});
}

TEST_P(BackendConformanceTest, projectionFollowsAdvertisedCapability)
{
    const auto& capabilities = database.getBackendCapabilities();
    orm::ProjectionQuery<models::ModelWithId, conformance_models::Projection> query;
    query.project(as("id", col("id")), as("name", col("field2")));

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
    const auto& capabilities = database.getBackendCapabilities();

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
    query.project(as("value", col("field1")));

    expectDatabaseError([this, &query]() { (void)database.select(query); }, orm::DatabaseErrorCode::Conversion,
                        GetParam().type, "select projection");
}

TEST_P(BackendConformanceTest, duplicatePrimaryKeyReportsConstraintError)
{
    const auto& capabilities = database.getBackendCapabilities();

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
    const auto& capabilities = database.getBackendCapabilities();

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
    query.where(col("id") == 1);
    const auto rows = database.select(query);

    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0].field3.id, target.id);
    EXPECT_EQ(rows[0].field3.field1, target.field1);
    EXPECT_EQ(rows[0].field3.field2, target.field2);
}

TEST_P(BackendConformanceTest, oneToManyPredicateAndUnlinkFollowAdvertisedCapabilities)
{
    const auto& capabilities = database.getBackendCapabilities();
    const auto author = collection_models::Author{1, "author", {}};
    const auto book = collection_models::Book{10, "portable-book", std::nullopt};

    if (not capabilities.relations.oneToMany)
    {
        expectDatabaseError([this, &author, &book]() { (void)database.link(author, "books", book); },
                            orm::DatabaseErrorCode::UnsupportedFeature, GetParam().type, "link relation");
        return;
    }

    if (not capabilities.mutations.update)
    {
        expectDatabaseError([this, &author, &book]() { (void)database.link(author, "books", book); },
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
        expectDatabaseError([this, &author, &book]() { (void)database.link(author, "books", book); },
                            orm::DatabaseErrorCode::AffectedRowsUnavailable, GetParam().type, "link relation");
        return;
    }

    ASSERT_EQ(database.link(author, "books", book), 1);

    orm::Query<collection_models::Author> query;
    query.where(any("books", col("title") == book.title));

    if (not capabilities.query.collectionPredicates or not capabilities.relations.collectionPredicates)
    {
        expectDatabaseError([this, &query]() { (void)database.select(query); },
                            orm::DatabaseErrorCode::UnsupportedFeature, GetParam().type, "select");
        EXPECT_EQ(database.unlink(author, "books", book), 1);
        return;
    }

    const auto linkedAuthors = database.select(query);
    ASSERT_EQ(linkedAuthors.size(), 1);
    EXPECT_EQ(linkedAuthors[0].id, author.id);
    EXPECT_FALSE(linkedAuthors[0].books.isLoaded());

    EXPECT_EQ(database.unlink(author, "books", book), 1);
    EXPECT_TRUE(database.select(query).empty());
}

TEST_P(BackendConformanceTest, collectionRelationFollowsAdvertisedCapabilities)
{
    const auto& capabilities = database.getBackendCapabilities();

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
        expectDatabaseError([this, &user, &role]() { (void)database.link(user, "roles", role); },
                            orm::DatabaseErrorCode::UnsupportedFeature, GetParam().type, "link relation");
        return;
    }

    if (capabilities.mutations.affectedRows != orm::db::AffectedRowsSupport::Reliable)
    {
        expectDatabaseError([this, &user, &role]() { (void)database.link(user, "roles", role); },
                            orm::DatabaseErrorCode::AffectedRowsUnavailable, GetParam().type, "link relation");
        return;
    }

    EXPECT_EQ(database.link(user, "roles", role), 1);
    orm::Query<collection_models::User> query;
    query.include("roles");

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
    const auto& capabilities = database.getBackendCapabilities();

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
        expectDatabaseError([this, &owner, &tag]() { (void)database.link(owner, "tags", tag); },
                            orm::DatabaseErrorCode::UnsupportedFeature, GetParam().type, "link relation");
        return;
    }

    if (capabilities.mutations.affectedRows != orm::db::AffectedRowsSupport::Reliable)
    {
        expectDatabaseError([this, &owner, &tag]() { (void)database.link(owner, "tags", tag); },
                            orm::DatabaseErrorCode::AffectedRowsUnavailable, GetParam().type, "link relation");
        return;
    }

    ASSERT_EQ(database.link(owner, "tags", tag), 1);

    if (capabilities.relations.collectionIncludes)
    {
        orm::Query<collection_models::CompositeOwner> query;
        query.where(col("tenant") == owner.tenant and col("id") == owner.id).include("tags");
        const auto owners = database.select(query);

        ASSERT_EQ(owners.size(), 1);
        ASSERT_TRUE(owners[0].tags.isLoaded());
        ASSERT_EQ(owners[0].tags.size(), 1);
        EXPECT_EQ(owners[0].tags[0].scope, tag.scope);
        EXPECT_EQ(owners[0].tags[0].id, tag.id);
    }

    if (not capabilities.mutations.remove)
    {
        expectDatabaseError([this, &owner, &tag]() { (void)database.unlink(owner, "tags", tag); },
                            orm::DatabaseErrorCode::UnsupportedFeature, GetParam().type, "unlink relation");
        return;
    }

    EXPECT_EQ(database.unlink(owner, "tags", tag), 1);
    EXPECT_EQ(database.unlink(owner, "tags", tag), 0);
}

TEST_P(BackendConformanceTest, explicitTransactionRollbackRestoresPreviousState)
{
    const auto& capabilities = database.getBackendCapabilities();

    if (not capabilities.transactions)
    {
        expectDatabaseError([this]() { database.beginTransaction(); }, orm::DatabaseErrorCode::UnsupportedFeature,
                            GetParam().type, "begin transaction");
        return;
    }

    if (not canPopulateSomeDataModel(capabilities))
    {
        database.beginTransaction();
        EXPECT_NO_THROW(database.rollbackTransaction());
        return;
    }

    createTable<models::SomeDataModel>();

    database.beginTransaction();
    database.insert(models::SomeDataModel{1, "rolled-back", 1.0});
    database.rollbackTransaction();

    orm::Query<models::SomeDataModel> query;
    EXPECT_TRUE(database.select(query).empty());
}

TEST_P(BackendConformanceTest, explicitTransactionCommitPersistsChanges)
{
    const auto& capabilities = database.getBackendCapabilities();

    if (not capabilities.transactions)
    {
        expectDatabaseError([this]() { database.beginTransaction(); }, orm::DatabaseErrorCode::UnsupportedFeature,
                            GetParam().type, "begin transaction");
        return;
    }

    if (not canPopulateSomeDataModel(capabilities))
    {
        GTEST_SKIP() << "Schema/type/insert rejection is covered by other conformance tests";
    }

    createTable<models::SomeDataModel>();
    database.beginTransaction();
    database.insert(models::SomeDataModel{1, "committed", 1.0});
    database.commitTransaction();

    orm::Query<models::SomeDataModel> query;
    query.where(col("field1") == 1);
    const auto rows = database.select(query);

    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0].field2, "committed");
}

TEST_P(BackendConformanceTest, orderedLimitFollowsAdvertisedCapability)
{
    const auto& capabilities = database.getBackendCapabilities();
    orm::Query<models::SomeDataModel> query;
    query.orderBy(asc(col("field1"))).limit(2);

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
    const auto& capabilities = database.getBackendCapabilities();
    orm::Query<models::ModelWithId> query;
    query.groupBy(col("field2")).having(countAll() == 2).orderBy(asc(col("field2")));

    if (supportsModelWithId(capabilities) and (not capabilities.query.groupBy or not capabilities.query.having))
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

TEST_P(BackendConformanceTest, orderedOffsetWithoutLimitReturnsRemainingRows)
{
    const auto& capabilities = database.getBackendCapabilities();
    orm::Query<models::SomeDataModel> query;
    query.orderBy(asc(col("field1"))).offset(1);

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
    const auto& capabilities = database.getBackendCapabilities();

    if (not canPopulateSomeDataModel(capabilities))
    {
        GTEST_SKIP() << "Schema/type/insert rejection is covered by other conformance tests";
    }

    createTable<models::SomeDataModel>();
    database.insert(std::vector<models::SomeDataModel>{{1, "one", 1.0}, {2, "two", 2.0}, {3, "three", 3.0}});

    orm::Update<models::SomeDataModel> update;
    update.set(col("field2"), "updated").where(col("field1") >= 2);

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
        expectDatabaseError([this]() { (void)database.remove<models::SomeDataModel>(col("field1") == 1); },
                            orm::DatabaseErrorCode::UnsupportedFeature, GetParam().type, "remove");
    }
    else if (capabilities.mutations.affectedRows != orm::db::AffectedRowsSupport::Reliable)
    {
        expectDatabaseError([this]() { (void)database.remove<models::SomeDataModel>(col("field1") == 1); },
                            orm::DatabaseErrorCode::AffectedRowsUnavailable, GetParam().type, "remove");
    }
    else
    {
        EXPECT_EQ(database.remove<models::SomeDataModel>(col("field1") == 1), 1);
    }
}

INSTANTIATE_TEST_SUITE_P(DatabaseTest, BackendConformanceTest, conformanceBackendTestConfigs, backendTestName);
