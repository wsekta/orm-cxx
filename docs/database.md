# Database

1. [Connect](#connect)
2. [Create table](#create-table)
3. [Delete table](#delete-table)
4. [Insert objects](#insert-objects)
5. [Query objects](#select-objects)

## Connect

To connect to database create its object and connect it with standard connection string:

```cpp
orm::Database database;
database.connect("sqlite3://test.db");
```

## Create table

To create table in database use `createTable` method and pass model as template argument:

```cpp
struct ObjectModel
{
    int id;
    std::string name;
    std::string email;
    std::string password;
    std::string created_at;
    std::string updated_at;
};

database.createTable<ObjectModel>();
```

## Delete table

To delete table from database use `deleteTable` method and pass model as template argument:

```cpp
database.deleteTable<ObjectModel>();
```

## Insert objects

To insert objects into database use `insert` method and pass vector of objects as argument:

```cpp
std::vector<ObjectModel> objects{
    {1, "name", "email", "password", "created_at", "updated_at"}, 
    {2, "name2", "email2", "password2", "created_at2", "updated_at2"}
};

database.insert(objects);
```

You can also insert single object:

```cpp
ObjectModel object{1, "name", "email", "password", "created_at", "updated_at"};

database.insert(object);
```

## Query objects

To select objects from database use `select` method and pass [select](select.md) as argument:

```cpp
orm::Query<ObjectModel> select;

auto queriedObjects = database.query(select);
```
