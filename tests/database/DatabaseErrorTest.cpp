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

static_assert(std::is_base_of_v<std::runtime_error, orm::DatabaseError>);

TEST(DatabaseErrorTest, shouldExposeStructuredBackendContext)
{
    const auto error = orm::DatabaseError{
        orm::DatabaseErrorCode::Constraint,
        orm::db::BackendType::Sqlite,
        "insert",
        "Database constraint violation",
        std::string{"19"},
    };

    EXPECT_EQ(error.getCode(), orm::DatabaseErrorCode::Constraint);
    EXPECT_EQ(error.getBackendType(), orm::db::BackendType::Sqlite);
    EXPECT_EQ(error.getOperation(), "insert");
    EXPECT_EQ(error.getNativeCode(), std::optional<std::string>{"19"});
    EXPECT_STREQ(error.what(), "Database constraint violation");
}

TEST(DatabaseErrorTest, nativeCodeShouldBeOptional)
{
    const auto error = orm::DatabaseError{
        orm::DatabaseErrorCode::NotConnected,
        orm::db::BackendType::Empty,
        "select",
        "Database is not connected",
    };

    EXPECT_FALSE(error.getNativeCode().has_value());
}

TEST(DatabaseErrorTest, shouldRemainCatchableAsRuntimeError)
{
    EXPECT_THROW((throw orm::DatabaseError{orm::DatabaseErrorCode::Transaction, orm::db::BackendType::Sqlite, "commit",
                                           "Transaction commit failed"}),
                 std::runtime_error);
}

TEST(DatabaseErrorTest, diagnosticShouldContainOnlyTheSanitizedMessage)
{
    const auto connectionString = std::string{"sqlite3://secret-database"};
    const auto parameterValue = std::string{"secret-parameter"};
    const auto error = orm::DatabaseError{
        orm::DatabaseErrorCode::Connection,
        orm::db::BackendType::Sqlite,
        "connect",
        "Database connection failed",
    };
    const auto diagnostic = std::string{error.what()};

    EXPECT_EQ(diagnostic, "Database connection failed");
    EXPECT_EQ(diagnostic.find(connectionString), std::string::npos);
    EXPECT_EQ(diagnostic.find(parameterValue), std::string::npos);
}
