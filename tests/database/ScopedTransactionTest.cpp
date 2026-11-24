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

namespace
{
using ScopedSchema = orm::Schema<models::SomeDataModel>;

static_assert(std::same_as<decltype(std::declval<orm::Database&>().scopedTransaction()), orm::ScopedTransaction>);
static_assert(not std::is_copy_constructible_v<orm::ScopedTransaction>);
static_assert(not std::is_move_constructible_v<orm::ScopedTransaction>);
static_assert(not std::is_copy_assignable_v<orm::ScopedTransaction>);
static_assert(not std::is_move_assignable_v<orm::ScopedTransaction>);
static_assert(std::is_nothrow_destructible_v<orm::ScopedTransaction>);
} // namespace

TEST(ScopedTransactionTest, sqliteCommitMakesWritesVisible)
{
    orm::Database connection{orm::SqliteConfig{.database = orm::SqliteMemory{}}};
    auto database = connection.orm<ScopedSchema>();
    database.createTable<models::SomeDataModel>();

    {
        auto transaction = connection.scopedTransaction();
        database.insert(models::SomeDataModel{1, "one", 1.0});
        transaction.commit();
    }

    orm::Query<models::SomeDataModel> query;
    const auto rows = database.select(query);
    ASSERT_EQ(rows.size(), 1U);
    EXPECT_EQ(rows.front().field1, 1);
    EXPECT_EQ(rows.front().field2, "one");
}

TEST(ScopedTransactionTest, onlyTheGuardCanFinishItsTransaction)
{
    orm::Database connection{orm::SqliteConfig{.database = orm::SqliteMemory{}}};
    auto transaction = connection.scopedTransaction();
    EXPECT_THROW(connection.beginTransaction(), orm::DatabaseError);
    EXPECT_THROW((void)connection.scopedTransaction(), orm::DatabaseError);
    EXPECT_THROW(connection.commitTransaction(), orm::DatabaseError);
    EXPECT_THROW(connection.rollbackTransaction(), orm::DatabaseError);
    EXPECT_NO_THROW(transaction.rollback());
    EXPECT_THROW(transaction.commit(), orm::DatabaseError);
    EXPECT_THROW(transaction.rollback(), orm::DatabaseError);

    connection.beginTransaction();
    EXPECT_THROW((void)connection.scopedTransaction(), orm::DatabaseError);
    EXPECT_NO_THROW(connection.rollbackTransaction());
}

TEST(ScopedTransactionTest, retiredGuardCannotFinishTheNewTransaction)
{
    const orm::SqliteConfig config{.database = orm::SqliteMemory{}};
    orm::Database connection{config};
    auto newTransaction = [&]() -> orm::ScopedTransaction
    {
        auto oldTransaction = connection.scopedTransaction();
        connection.disconnect();
        EXPECT_THROW(oldTransaction.commit(), orm::DatabaseError);
        connection.connect(config);
        EXPECT_THROW(oldTransaction.rollback(), orm::DatabaseError);
        return connection.scopedTransaction();
    }();
    EXPECT_NO_THROW(newTransaction.commit());
}

TEST(ScopedTransactionTest, scopeExitAndExplicitRollbackDiscardWrites)
{
    orm::Database connection{orm::SqliteConfig{.database = orm::SqliteMemory{}}};
    auto database = connection.orm<ScopedSchema>();
    database.createTable<models::SomeDataModel>();
    {
        auto transaction = connection.scopedTransaction();
        database.insert(models::SomeDataModel{1, "scope exit", 1.0});
    }
    {
        auto transaction = connection.scopedTransaction();
        database.insert(models::SomeDataModel{2, "explicit rollback", 2.0});
        transaction.rollback();
    }
    orm::Query<models::SomeDataModel> query;
    EXPECT_TRUE(database.select(query).empty());
}
