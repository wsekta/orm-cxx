#include <optional>

#include "orm-cxx/orm.hpp"

struct ObjectModel
{
    std::optional<int> field1;
    std::string field2; // not null
    inline static const std::vector<std::string> id_columns = {"field1", "field2"};
};

int main()
{
    // connect with standard connection string
    orm::Database database;
    database.connect("sqlite3://test.db");

    // drop table if exists
    database.deleteTable<ObjectModel>();

    // create table in database
    database.createTable<ObjectModel>();

    // create objects and insert them into table
    std::vector<ObjectModel> objects{{1, "test"}, {std::nullopt, "text"}};
    database.insertObjects(objects);

    // query objects from database
    orm::Query<ObjectModel> query;
    auto queriedObjects = database.executeQuery(query);

    return 0;
}