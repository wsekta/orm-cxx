module;

#include "tests/UnitTestPrelude.hpp"

module orm;

import :internal;
import :test_support;
import :foundation;
import :model;
import :expressions;
import :dynamic_query;
import :database;

using namespace orm::test::fixtures;

namespace context_models
{
struct Record
{
    int id;
    std::string value;
    inline static constexpr orm::reflection::FixedString table_name{"context_records"};
};

struct Event
{
    int id;
    std::string value;
    inline static constexpr orm::reflection::FixedString table_name{"context_events"};
};

using Records = orm::Schema<Record>;
using Events = orm::Schema<Event>;
using Combined = orm::Schema<Record, Event>;
} // namespace context_models

class OrmContextTest : public DatabaseTest<context_models::Combined>
{
};

TEST(OrmContextBindingTest, assignmentRebindsTheViewToAnotherDatabase)
{
    using namespace context_models;
    orm::Database first;
    orm::Database second;
    first.connect("sqlite3://:memory:");
    second.connect("sqlite3://:memory:");
    auto firstRecords = first.orm<Records>();
    auto secondRecords = second.orm<Records>();
    firstRecords.createTable<Record>();
    secondRecords.createTable<Record>();
    firstRecords.insert(Record{1, "first"});
    secondRecords.insert(Record{2, "second"});
    auto view = firstRecords;
    orm::Query<Record> query;
    ASSERT_EQ(view.select(query).size(), 1);
    EXPECT_EQ(view.select(query).front().id, 1);
    view = secondRecords;
    EXPECT_EQ(view.select(query).front().id, 2);
    view = std::move(firstRecords);
    EXPECT_EQ(view.select(query).front().id, 1);
}

TEST_P(OrmContextTest, differentSchemasShareOneTransactionAndSeeUncommittedRows)
{
    using namespace context_models;
    createTable<Record>();
    createTable<Event>();
    auto records = connection.orm<Records>();
    auto events = connection.orm<Events>();
    orm::Query<Record> query;
    orm::Query<Event> eventQuery;

    connection.beginTransaction();
    records.insert(Record{1, "record"});
    events.insert(Event{1, "event"});
    EXPECT_EQ(database.select(query).size(), 1);
    EXPECT_EQ(database.select(eventQuery).size(), 1);
    connection.commitTransaction();

    EXPECT_EQ(records.select(query).size(), 1);
    EXPECT_EQ(events.select(eventQuery).size(), 1);
}

TEST_P(OrmContextTest, rollbackUndoesWritesFromAllSchemas)
{
    using namespace context_models;
    createTable<Record>();
    createTable<Event>();
    auto records = connection.orm<Records>();
    auto events = connection.orm<Events>();

    connection.beginTransaction();
    records.insert(Record{1, "record"});
    events.insert(Event{1, "event"});
    connection.rollbackTransaction();

    orm::Query<Record> query;
    orm::Query<Event> eventQuery;
    EXPECT_TRUE(records.select(query).empty());
    EXPECT_TRUE(events.select(eventQuery).empty());
}

TEST_P(OrmContextTest, copiesAndMovesBorrowTheSameSessionWithoutEndingItsTransaction)
{
    using namespace context_models;
    createTable<Record>();
    auto records = connection.orm<Records>();
    auto copy = records;
    auto moved = std::move(copy);
    copy = moved;
    moved = std::move(records);

    connection.beginTransaction();
    {
        auto temporary = copy;
        temporary.insert(Record{1, "temporary"});
    }
    orm::Query<Record> query;
    EXPECT_EQ(moved.select(query).size(), 1);
    EXPECT_EQ(copy.select(query).size(), 1);
    EXPECT_TRUE(connection.isConnected());
    connection.rollbackTransaction();
    EXPECT_TRUE(copy.select(query).empty());
}

TEST_P(OrmContextTest, contextCreatedBeforeConnectUsesTheCurrentSessionAfterReconnect)
{
    using namespace context_models;
    orm::Database owner;
    auto records = owner.orm<Records>();
    EXPECT_THROW(records.createTable<Record>(), orm::DatabaseError);
    owner.connect(GetParam().type, testConnectionString());
    records.createTable<Record>();
    records.insert(Record{1, "before"});
    owner.disconnect();
    orm::Query<Record> query;
    EXPECT_THROW((void)records.select(query), orm::DatabaseError);
    owner.connect(GetParam().type, testConnectionString());
    records.createTable<Record>();
    records.insert(Record{2, "after"});
    query.where(orm::query::col<&Record::id>() == 2);
    const auto rows = records.select(query);
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows.front().value, "after");
    records.deleteTable<Record>();
    owner.disconnect();
}

TEST_P(OrmContextTest, postgresFailedTransactionIsSharedAcrossContexts)
{
    if (GetParam().type != orm::db::BackendType::Postgres)
        GTEST_SKIP() << "PostgreSQL aborts a transaction after a statement error";

    using namespace context_models;
    createTable<Record>();
    createTable<Event>();
    auto records = connection.orm<Records>();
    auto events = connection.orm<Events>();
    connection.beginTransaction();
    records.insert(Record{1, "record"});
    events.insert(Event{1, "event"});
    EXPECT_THROW(events.insert(Event{1, "duplicate"}), orm::DatabaseError);
    orm::Query<Record> query;
    EXPECT_THROW((void)records.select(query), orm::DatabaseError);
    EXPECT_THROW(connection.commitTransaction(), orm::DatabaseError);
    connection.rollbackTransaction();
    EXPECT_TRUE(records.select(query).empty());
    orm::Query<Event> eventQuery;
    EXPECT_TRUE(events.select(eventQuery).empty());
}

INSTANTIATE_TEST_SUITE_P(DatabaseTest, OrmContextTest, conformanceBackendTestConfigs, backendTestName);
