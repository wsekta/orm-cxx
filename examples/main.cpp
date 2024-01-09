#include <optional>

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

    // defining table_name is optional
    inline static const std::string table_name = "object_model";
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