#include <optional>
#include <string>
#include <vector>

#include "orm-cxx/orm.hpp"

struct ObjectModel
{
    // INTEGER - if field is optional it will not be marked as NOT NULL
    std::optional<int> field1;

    // TEXT NOT NULL
    std::string field2;

    // Defining id_columns is optional.
    inline static constexpr auto id_columns = orm::primaryKey<&ObjectModel::field2>();

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

int main() // NOLINT(bugprone-exception-escape)
{
    using namespace orm::query;

    // connect with standard connection string
    using AppSchema = orm::Schema<ObjectModel>;
    orm::Database<AppSchema> database;
    database.connect("sqlite3://test.db");

    // drop table if exists
    database.deleteTable<ObjectModel>();

    // create table in database
    database.createTable<ObjectModel>();

    // create objects and insert them into table
    std::vector<ObjectModel> objects{{1, "test-1"}, {2, "test-2"}, {std::nullopt, "text"}};
    database.insert(objects);

    // full-model select returns std::vector<ObjectModel>
    orm::Query<ObjectModel> fullQuery;
    fullQuery.limit(10).offset(5);
    auto queriedObjects = database.select(fullQuery);

    // grouping and HAVING keep the full-model result type
    orm::Query<ObjectModel> groupedQuery;
    groupedQuery.where(col("field1").isNotNull())
        .groupBy(col("field2"))
        .having(countAll() > 0)
        .andHaving(avg(col("field1")) >= 1.0);
    auto groupedObjects = database.select(groupedQuery);

    // projection select returns a flat DTO
    orm::ProjectionQuery<ObjectModel, ObjectSummary> summaryQuery;
    summaryQuery.project(as("number", col("field1")), as("name", col("field2"))).orderBy(asc(col("field2")));
    auto summaries = database.select(summaryQuery);

    // aggregate projection returns a flat DTO with aggregate fields
    orm::ProjectionQuery<ObjectModel, ObjectStats> statsQuery;
    statsQuery.project(as("name", col("field2")), as("rows", countAll()), as("averageNumber", avg(col("field1"))))
        .groupBy(col("field2"))
        .having(countAll() > 0);
    auto stats = database.select(statsQuery);

    return 0;
}
