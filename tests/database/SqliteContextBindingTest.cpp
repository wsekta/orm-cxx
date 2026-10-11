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
