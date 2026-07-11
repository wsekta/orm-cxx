#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "orm-cxx/orm.hpp"

struct ObjectModel
{
    // INTEGER - if field is optional it will not be marked as NOT NULL
    std::optional<int> field1;

    // TEXT NOT NULL
    std::string field2;

    // defining id_columns is optional
    inline static const std::vector<std::string> id_columns = {"field1", "field2"};

    // other way to define id column - will be over writen by using id_columns
    // int id;

    // defining table_name is optional, adding it will overwrite default table name
    inline static constexpr std::string_view table_name = "object_model";

    // defining columns_names is optional, adding it will overwrite default columns names
    // not all columns have to be defined, others will get default names
    inline static const std::map<std::string, std::string> columns_names = {{"field1", "some_field1_name"},
                                                                            {"field2", "some_field2_name"}};
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

int main()
{
    using namespace orm::query;

    // connect with standard connection string
    orm::Database database;
    database.connect("sqlite3://test.db");

    // drop table if exists
    database.deleteTable<ObjectModel>();

    // create table in database
    database.createTable<ObjectModel>();

    // create objects and insert them into table
    std::vector<ObjectModel> objects{{1, "test"}, {2, "test"}, {std::nullopt, "text"}};
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
