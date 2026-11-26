module;

#include "tests/UnitTestPrelude.hpp"

module orm;

import :test_support;
import :foundation;
import :model;
import :expressions;
import :dynamic_query;
import :static_plan;
import :database;
import :perf_test_seam;

using namespace orm::test::fixtures;

class InsertManyPerfSeamTest : public DatabaseTest<orm::Schema<models::ModelWithId>>
{
};

TEST_P(InsertManyPerfSeamTest, forcedChunkSizeOneRunsTheProductionPathOverManyRows)
{
    createTable<models::ModelWithId>();
    std::vector<models::ModelWithId> rows;
    for (int id = 1; id <= 40; ++id)
    {
        rows.push_back(models::ModelWithId{id, id, std::format("row-{}", id)});
    }

    orm::detail::perf_seam::insertManyWithChunkRowsForTest(database, std::span<const models::ModelWithId>{rows}, 1);

    orm::Query<models::ModelWithId> query;
    query.orderBy(orm::query::asc(orm::query::col<&models::ModelWithId::id>()));
    const auto stored = database.select(query);
    ASSERT_EQ(stored.size(), rows.size());
    for (std::size_t index = 0; index < rows.size(); ++index)
    {
        EXPECT_EQ(stored[index].id, rows[index].id);
        EXPECT_EQ(stored[index].field2, rows[index].field2);
    }
}

TEST_P(InsertManyPerfSeamTest, rejectsAZeroChunkOverride)
{
    const std::array rows{models::ModelWithId{1, 2, "unused"}};
    EXPECT_THROW(
        orm::detail::perf_seam::insertManyWithChunkRowsForTest(database, std::span<const models::ModelWithId>{rows}, 0),
        std::invalid_argument);
}

INSTANTIATE_TEST_SUITE_P(Postgresql, InsertManyPerfSeamTest, ::testing::Values(postgresqlBackendTestConfig),
                         backendTestName);
