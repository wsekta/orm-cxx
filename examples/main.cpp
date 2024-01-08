#include "orm-cxx/orm.hpp"

#include <optional>

struct ObjectModel
{
  std::optional<int> filed1;
  std::string field2; //not null
};

int main()
{
    //connect with standard connection string
    orm::Database database;
    database.connect("sqlite3://test.db");

    //create table in database
    database.createTable<ObjectModel>();

    //create objects and insert them into table
    std::vector<ObjectModel> objects{{1, "test"}, {std::nullopt,"text"}};
    database.insertObjects(objects);

    //query objects from database
    orm::Query<ObjectModel> query;
    auto queriedObjects = database.executeQuery(query);

    return 0;
}