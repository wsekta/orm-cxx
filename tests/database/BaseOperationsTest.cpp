#include <map>
#include <string>
#include <string_view>
#include <type_traits>

#include "DatabaseTest.hpp"

static_assert(not std::is_copy_constructible_v<orm::Database>);
static_assert(not std::is_copy_assignable_v<orm::Database>);
static_assert(not std::is_move_constructible_v<orm::Database>);
static_assert(not std::is_move_assignable_v<orm::Database>);

namespace base_operations_models
{
struct ReservedIdentifierModel
{
    inline static constexpr std::string_view table_name = "order";
    inline static const std::map<std::string, std::string> columns_names = {
        {"id", "select"},
        {"value", "group"},
    };

    int id;
    std::string value;
};
} // namespace base_operations_models

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
} // namespace

class BaseOperationsTest : public DatabaseTest
{
};

TEST(DatabaseConnectionLifecycleTest, shouldAutoDetectBackendFromConnectionString)
{
    orm::Database database;

    database.connect("sqlite3://:memory:");

    EXPECT_TRUE(database.isConnected());
    EXPECT_EQ(database.getBackendType(), orm::db::BackendType::Sqlite);
    database.disconnect();
}

TEST(DatabaseConnectionLifecycleTest, explicitBackendShouldRejectMismatchedConnectionString)
{
    orm::Database database;

    expectDatabaseError([&database]()
                        { database.connect(orm::db::BackendType::Sqlite, "postgresql://localhost/test"); },
                        orm::DatabaseErrorCode::Connection, orm::db::BackendType::Sqlite, "connect");

    EXPECT_FALSE(database.isConnected());
    EXPECT_EQ(database.getBackendType(), orm::db::BackendType::Empty);
}

TEST(DatabaseConnectionLifecycleTest, unknownConnectionSchemeShouldReportUnsupportedBackend)
{
    orm::Database database;

    expectDatabaseError([&database]() { database.connect("unknown://database"); },
                        orm::DatabaseErrorCode::UnsupportedBackend, orm::db::BackendType::Empty, "connect");
}

TEST(DatabaseConnectionLifecycleTest, unimplementedExplicitBackendShouldReportUnsupportedBackend)
{
    orm::Database database;

    expectDatabaseError([&database]()
                        { database.connect(orm::db::BackendType::Postgres, "postgresql://localhost/test"); },
                        orm::DatabaseErrorCode::UnsupportedBackend, orm::db::BackendType::Postgres, "connect");
}

TEST(DatabaseConnectionLifecycleTest, secondConnectShouldReportAlreadyConnected)
{
    orm::Database database;
    database.connect("sqlite3://:memory:");

    expectDatabaseError([&database]() { database.connect("sqlite3://:memory:"); },
                        orm::DatabaseErrorCode::AlreadyConnected, orm::db::BackendType::Sqlite, "connect");

    EXPECT_TRUE(database.isConnected());
    database.disconnect();
}

TEST(DatabaseConnectionLifecycleTest, capabilitiesWithoutConnectionShouldReportNotConnected)
{
    orm::Database database;

    expectDatabaseError([&database]() { (void)database.getBackendCapabilities(); },
                        orm::DatabaseErrorCode::NotConnected, orm::db::BackendType::Empty, "database operation");
}

TEST(DatabaseConnectionLifecycleTest, operationWithoutConnectionShouldReportNotConnected)
{
    orm::Database database;

    expectDatabaseError([&database]() { database.createTable<models::SomeDataModel>(); },
                        orm::DatabaseErrorCode::NotConnected, orm::db::BackendType::Empty, "database operation");
}

TEST_P(BaseOperationsTest, shouldConnectToDatabase)
{
    EXPECT_TRUE(database.isConnected());
    EXPECT_EQ(database.getBackendType(), GetParam().type);
}

TEST_P(BaseOperationsTest, shouldCreateTable)
{
    EXPECT_NO_THROW(createTable<models::SomeDataModel>());
}

TEST_P(BaseOperationsTest, shouldDeleteTable)
{
    createTable<models::SomeDataModel>();

    EXPECT_NO_THROW(database.deleteTable<models::SomeDataModel>());
}

TEST_P(BaseOperationsTest, sholdInsertObjects)
{
    createTable<models::SomeDataModel>();

    EXPECT_NO_THROW(database.insert(models::SomeDataModel{}));
}

TEST_P(BaseOperationsTest, shouldQuoteMappedReservedIdentifiersAcrossCrudCommands)
{
    createTable<base_operations_models::ReservedIdentifierModel>();
    database.insert(base_operations_models::ReservedIdentifierModel{1, "before"});

    orm::Query<base_operations_models::ReservedIdentifierModel> select;
    select.where(orm::query::col("value") == "before");
    auto rows = database.select(select);

    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0].id, 1);

    orm::Update<base_operations_models::ReservedIdentifierModel> update;
    update.set(orm::query::col("value"), "after").where(orm::query::col("id") == 1);
    EXPECT_EQ(database.update(update), 1);
    EXPECT_EQ(database.remove<base_operations_models::ReservedIdentifierModel>(orm::query::col("value") == "after"), 1);
}

INSTANTIATE_TEST_SUITE_P(DatabaseTest, BaseOperationsTest, backendTestConfigs, backendTestName);
