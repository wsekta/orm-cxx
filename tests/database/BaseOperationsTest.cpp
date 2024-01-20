#include "DatabaseTest.hpp"

class BaseOperationsTest : public ::testing::TestWithParam<std::string>
{
public:
    orm::Database database;
};

TEST_P(BaseOperationsTest, shouldConnectToDatabase)
{
    EXPECT_NO_THROW(database.connect(GetParam()));

    EXPECT_EQ(database.getBackendType(), orm::db::BackendType::Sqlite);
}

TEST_P(BaseOperationsTest, shouldCreateTable)
{
    database.connect(GetParam());

    EXPECT_NO_THROW(database.createTable<models::SomeDataModel>());
}

TEST_P(BaseOperationsTest, shouldDeleteTable)
{
    database.connect(GetParam());

    database.createTable<models::SomeDataModel>();

    EXPECT_NO_THROW(database.deleteTable<models::SomeDataModel>());
}

TEST_P(BaseOperationsTest, sholdInsertObjects)
{
    database.connect(GetParam());

    database.createTable<models::SomeDataModel>();

    EXPECT_NO_THROW(database.insert(models::SomeDataModel{}));
}

INSTANTIATE_TEST_SUITE_P(DatabaseTest, BaseOperationsTest, connectionStrings);