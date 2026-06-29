# Database

1. [Connect](#connect)
2. [Create table](#create-table)
3. [Delete table](#delete-table)
4. [Insert objects](#insert-objects)
5. [Query objects](#select-objects)
6. [Update objects](#update-objects)
7. [Remove objects](#remove-objects)
8. [Transactions](#transactions)

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

For models with an auto-increment primary key, the generated `INSERT` statement omits that primary-key column and
SQLite assigns the value:

```cpp
struct User
{
    inline static const std::vector<std::string> auto_increment_columns = {"id"};

    int id;
    std::string name;
};

database.createTable<User>();
database.insert(User{0, "Ann"});
```

`insert` does not mutate the passed object. Select the row after insertion if you need the generated id.

Optional fields and optional one-to-one relations store `std::nullopt` as SQL `NULL`:

```cpp
struct Profile
{
    int id;
    std::string city;
};

struct User
{
    int id;
    std::optional<Profile> profile;
};

database.insert(User{1, std::nullopt});
```

## Query objects

To select objects from database use `select` method and pass [query](query.md) as argument:

```cpp
using namespace orm::query;

orm::Query<ObjectModel> query;
query.where(col("name").like("name%"))
     .orderBy(asc(col("id")))
     .limit(10);

auto queriedObjects = database.select(query);
```

## Update objects

To update rows, build an `orm::Update<Model>` with one or more assignments and a required predicate:

```cpp
using namespace orm::query;

orm::Update<ObjectModel> update;
update.set(col("email"), "new-email@example.com")
      .set(col("updated_at"), "updated_at")
      .where(col("id") == 1);

std::size_t updatedRows = database.update(update);
```

Use `std::nullopt` to store `NULL` in nullable columns:

```cpp
orm::Update<ObjectModel> clearEmail;
clearEmail.set(col("email"), std::nullopt)
          .where(col("id") == 1);
```

`update` returns the number of affected rows. Calling it without a `where` predicate or without assignments throws
`std::invalid_argument`. Assigning `NULL` to a non-nullable column also throws before executing SQL.

## Remove objects

To delete rows, call `remove` with the model type and a required predicate:

```cpp
using namespace orm::query;

std::size_t removedRows = database.remove<ObjectModel>(col("id") == 1);
```

`remove` returns the number of affected rows. There is no unfiltered public delete-row API.

## Transactions

To use transaction use database's `transactionBegin` method followed by `transactionCommit` or `transactionRollback`:

```cpp
database.transactionBegin();

database.insert(objects);

database.transactionCommit();
```

or:

```cpp
database.transactionBegin();

database.insert(objects);

database.transactionRollback();
```
