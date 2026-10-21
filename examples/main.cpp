#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

import orm;

#include "Connection.hpp"

struct ObjectModel
{
    // INTEGER - if field is optional it will not be marked as NOT NULL
    std::optional<int> field1;

    // TEXT NOT NULL
    std::string field2;

    // Defining id_columns is optional.
    inline static constexpr auto id_columns = orm::primaryKey<&ObjectModel::field2>();
    inline static constexpr auto string_lengths = orm::stringLengths(orm::stringLength<&ObjectModel::field2, 191>());

    // Another way to define the default primary key is an `int id` field.
    // int id;

    // FixedString keeps mapping metadata available during constant evaluation.
    inline static constexpr orm::reflection::FixedString table_name{"object_model"};

    // Unlisted fields retain their reflected names.
    inline static constexpr auto columns_names =
        orm::columnNames(orm::columnName<&ObjectModel::field1, "some_field1_name">(),
                         orm::columnName<&ObjectModel::field2, "some_field2_name">());
};

struct ObjectSummary
{
    std::optional<int> number;
    std::string name;
};

struct ObjectStats
{
    std::string name;
    long long rows;
    std::optional<double> averageNumber;
};

int main(int argc, char** argv) // NOLINT(bugprone-exception-escape)
{
    using namespace orm::query;

    // Open synchronously using a typed backend configuration.
    using AppSchema = orm::Schema<ObjectModel>;
    auto database = makeExampleDatabase(argc, argv);
    auto context = database.orm<AppSchema>();

    // drop table if exists
    context.deleteTable<ObjectModel>();

    // create table in database
    context.createTable<ObjectModel>();

    // create objects and insert them into table
    std::vector<ObjectModel> objects{{1, "test-1"}, {2, "test-2"}, {std::nullopt, "text"}};
    context.insert(objects);

    // full-model select returns std::vector<ObjectModel>
    orm::Query<ObjectModel> fullQuery;
    fullQuery.limit(10).offset(5);
    auto queriedObjects = context.select(fullQuery);

    // projection select returns a flat DTO
    orm::ProjectionQuery<ObjectModel, ObjectSummary> summaryQuery;
    summaryQuery.project(as("number", col<&ObjectModel::field1>()), as("name", col<&ObjectModel::field2>()))
        .orderBy(asc(col<&ObjectModel::field2>()));
    auto summaries = context.select(summaryQuery);

    // aggregate projection returns a flat DTO with aggregate fields
    orm::ProjectionQuery<ObjectModel, ObjectStats> statsQuery;
    statsQuery
        .project(as("name", col<&ObjectModel::field2>()), as("rows", countAll<ObjectModel>()),
                 as("averageNumber", avg(col<&ObjectModel::field1>())))
        .groupBy(col<&ObjectModel::field2>())
        .having(countAll<ObjectModel>() > 0);
    auto stats = context.select(statsQuery);

    return 0;
}
