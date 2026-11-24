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

using namespace orm::test::fixtures;

namespace insert_many_guard_models
{
struct TextRecord
{
    int id;
    std::string text;
    std::int64_t big;

    inline static constexpr orm::reflection::FixedString table_name{"insert_many_guard_text"};
};
} // namespace insert_many_guard_models

using InsertManyGuardSchema = orm::Schema<models::ModelWithId, models::ModelWithOverwrittenId,
                                          models::ModelRelatedToCompositeIdModel, insert_many_guard_models::TextRecord>;

namespace
{
template <typename T>
auto selectAllOrderedById(orm::OrmContext<InsertManyGuardSchema>& database) -> std::vector<T>
{
    orm::Query<T> query;
    query.orderBy(orm::query::asc(orm::query::col<&T::id>()));
    return database.select(query);
}

template <typename T>
auto countRows(orm::OrmContext<InsertManyGuardSchema>& database) -> std::size_t
{
    orm::Query<T> query;
    return database.select(query).size();
}

auto makeModelWithIdRows(int count, int firstId = 1) -> std::vector<models::ModelWithId>
{
    std::vector<models::ModelWithId> rows;
    rows.reserve(static_cast<std::size_t>(count));

    for (int index = 0; index < count; ++index)
    {
        rows.push_back(models::ModelWithId{firstId + index, index, std::format("row-{}", index)});
    }

    return rows;
}
} // namespace

/**
 * Guard tests for the batch planner and error translation that are distinct from the parity cases:
 * composite relation-key parameter names, the encoded-value chunk split, and the exact error shape of
 * the guards this lane added.
 *
 * PostgreSQL only: the file intentionally has no SQLite instantiation.
 */
class InsertManyGuardTest : public DatabaseTest<InsertManyGuardSchema>
{
};

TEST_P(InsertManyGuardTest, compositeToOneParameterNamesAreBoundAcrossChunks)
{
    createTable<models::ModelWithOverwrittenId>();
    createTable<models::ModelRelatedToCompositeIdModel>();

    // ModelWithOverwrittenId declares (field1, field2) as a composite primary key, so each row binds
    // "field3_field1" and "field3_field2" instead of a single relation key.
    const std::vector<models::ModelWithOverwrittenId> parents{
        {10, 1, "key-one"},
        {20, 2, "key-two"},
    };
    database.insert(parents);

    std::vector<models::ModelRelatedToCompositeIdModel> rows;
    rows.reserve(35);
    for (int index = 0; index < 35; ++index)
    {
        rows.push_back(models::ModelRelatedToCompositeIdModel{index + 1, std::format("related-{}", index),
                                                              parents[static_cast<std::size_t>(index % 2)]});
    }

    ASSERT_NO_THROW(database.insertMany(rows));

    const auto stored = selectAllOrderedById<models::ModelRelatedToCompositeIdModel>(database);
    ASSERT_EQ(stored.size(), rows.size());

    for (const auto& row : stored)
    {
        const auto expected = parents[static_cast<std::size_t>(row.id - 1) % 2];
        EXPECT_EQ(row.field3.field1, expected.field1);
        EXPECT_EQ(row.field3.field2, expected.field2);
    }

    EXPECT_EQ(countRows<models::ModelWithOverwrittenId>(database), parents.size());
}

TEST_P(InsertManyGuardTest, oversizedEncodedStringIsRejectedWithoutRows)
{
    createTable<insert_many_guard_models::TextRecord>();
    const std::string oversized(4 * 1024 * 1024 + 1, 'x');
    const std::vector<insert_many_guard_models::TextRecord> rows{{1, oversized, 7}};

    try
    {
        database.insertMany(rows);
        FAIL() << "Expected orm::DatabaseError";
    }
    catch (const orm::DatabaseError& error)
    {
        EXPECT_EQ(error.getCode(), orm::DatabaseErrorCode::UnsupportedFeature);
        EXPECT_EQ(error.getBackendType(), orm::db::BackendType::Postgres);
        EXPECT_EQ(error.getOperation(), "insert many");
        // This guard is decided before the driver sees the row, so it has no native SQLSTATE.
        EXPECT_FALSE(error.getNativeCode().has_value());
    }

    EXPECT_EQ(countRows<insert_many_guard_models::TextRecord>(database), 0);
}

TEST_P(InsertManyGuardTest, encodedValueBudgetSplitsTheBatchAndKeepsEveryRow)
{
    createTable<insert_many_guard_models::TextRecord>();

    // Each row fits the 4 MiB encoded-parameter budget on its own, but three rows together do not.
    const std::string payload(1536 * 1024, 'z');
    const std::vector<insert_many_guard_models::TextRecord> rows{
        {1, payload, 1},
        {2, payload, 2},
        {3, payload, 3},
    };

    ASSERT_NO_THROW(database.insertMany(rows));

    const auto stored = selectAllOrderedById<insert_many_guard_models::TextRecord>(database);
    ASSERT_EQ(stored.size(), rows.size());

    for (std::size_t index = 0; index < rows.size(); ++index)
    {
        EXPECT_EQ(stored[index].id, rows[index].id);
        EXPECT_EQ(stored[index].text, payload);
        EXPECT_EQ(stored[index].big, rows[index].big);
    }
}

TEST_P(InsertManyGuardTest, ownTransactionConstraintFailureKeepsTheOriginalErrorShapeAndRollsBack)
{
    createTable<models::ModelWithId>();

    auto rows = makeModelWithIdRows(70);
    rows[40].id = rows[0].id;

    try
    {
        database.insertMany(rows);
        FAIL() << "Expected orm::DatabaseError";
    }
    catch (const orm::DatabaseError& error)
    {
        EXPECT_EQ(error.getCode(), orm::DatabaseErrorCode::Constraint);
        EXPECT_EQ(error.getBackendType(), orm::db::BackendType::Postgres);
        EXPECT_EQ(error.getOperation(), "insert many");
        ASSERT_TRUE(error.getNativeCode().has_value());
        EXPECT_EQ(error.getNativeCode().value(), "23505");
    }

    EXPECT_EQ(countRows<models::ModelWithId>(database), 0);

    // The rollback of the operation-owned transaction succeeded, so the connection stays usable.
    ASSERT_NO_THROW(database.insertMany(makeModelWithIdRows(2, 100)));
    EXPECT_EQ(countRows<models::ModelWithId>(database), 2);
}

INSTANTIATE_TEST_SUITE_P(Postgresql, InsertManyGuardTest, ::testing::Values(postgresqlBackendTestConfig),
                         backendTestName);
