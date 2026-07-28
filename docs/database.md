# Database

1. [Connect](#connect)
2. [Capabilities and errors](#capabilities-and-errors)
3. [Create table](#create-table)
4. [Delete table](#delete-table)
5. [Create and delete relation tables](#create-and-delete-relation-tables)
6. [Insert objects](#insert-objects)
7. [Link and unlink relations](#link-and-unlink-relations)
8. [Query objects](#query-objects)
9. [Update objects](#update-objects)
10. [Remove objects](#remove-objects)
11. [Transactions](#transactions)

## Connect

To connect to database create its object and connect it with standard connection string:

```cpp
orm::Database database;
database.connect("sqlite3://test.db");
```

Automatic selection requires exactly one registered backend to accept the
connection string. Select a known backend explicitly when desired:

```cpp
database.connect(orm::db::BackendType::Sqlite, "sqlite3://test.db");
```

PostgreSQL uses a `postgresql://` backend selector followed by SOCI's
keyword/value connection payload:

```cpp
database.connect(
    orm::db::BackendType::Postgres,
    "postgresql://host=localhost port=5432 dbname=application user=application password=secret");
```

This is not an RFC-style `postgresql://user@host/database` URI. Connection
strings are never copied into `DatabaseError` diagnostics. Keyword values with
whitespace are not supported by the vendored SOCI parser; use a PostgreSQL
passfile/`PGPASSFILE` for credentials that cannot be represented safely.

Use `isConnected()` and `getBackendType()` to inspect lifecycle state. Calling
`disconnect()` rolls back an active transaction before closing the session; the
same `Database` object can then connect again. A `Database` is intentionally
neither copyable nor movable because an active transaction is tied to its SOCI
session.

Both supported backends enforce generated foreign keys. SQLite connections
automatically enable `PRAGMA foreign_keys=ON`; PostgreSQL requires no equivalent
session setup. Foreign-key violations therefore fail immediately, and deleting
a many-to-many endpoint removes its junction rows through the generated
`ON DELETE CASCADE` rules. PostgreSQL uses the session's active `search_path`;
schema-qualified model names are not a public API in this release.

## Capabilities and errors

`getBackendCapabilities()` returns the selected backend's centralized schema,
query, mutation, relation, type, value-limit, and transaction feature profile.
Unsupported optional behavior is rejected before SQL execution with
`DatabaseErrorCode::UnsupportedFeature`.

Operations crossing the backend boundary throw `orm::DatabaseError`. Inspect
`getCode()`, `getBackendType()`, `getOperation()`, and the optional
`getNativeCode()` instead of parsing vendor message text. Diagnostics are
sanitized and do not contain connection strings or bound values.

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

## Create and delete relation tables

Base model tables and many-to-many junction tables have an explicit lifecycle.
Create both endpoint tables first, then create junction tables from the owning
model:

```cpp
database.createTable<User>();
database.createTable<Role>();
database.createRelationTables<User>();
```

`createRelationTables<T>()` creates only junction tables owned by `T`. It is a
no-op for one-to-many mappings and inverse many-to-many mappings. Repeated calls
are safe. Endpoint tables must already exist.

Drop junction tables before either endpoint table:

```cpp
database.deleteRelationTables<User>();
database.deleteTable<Role>();
database.deleteTable<User>();
```

`deleteRelationTables<T>()` is also idempotent and affects only junction tables
owned by `T`. `createTable`, `deleteTable`, `insert`, and row deletion never
recursively create, drop, or synchronize relation tables.

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
the selected database assigns the value:

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

`OneToMany` and `ManyToMany` wrapper fields are ignored by `insert`. Endpoint
objects must be inserted separately, followed by explicit `link` calls where a
stored relation is needed. There is no cascade-save.

## Link and unlink relations

Many-to-many mutations add or remove one junction row:

```cpp
User user{1, "Ada"};
Role admin{10, "admin"};

std::size_t linked = database.link(user, "roles", admin);
std::size_t unlinked = database.unlink(user, "roles", admin);
```

Each operation is idempotent: it returns `1` when the relation changed and `0`
when the database was already in the requested state. The same operations can
be called through an inverse many-to-many field.

For one-to-many, pass the parent, collection field name, and child:

```cpp
std::size_t assigned = database.link(author, "books", book);
std::size_t detached = database.unlink(author, "books", book);
```

`link` updates the child's mapped foreign key, including moving it from another
parent. `unlink` writes SQL `NULL`; it throws `std::invalid_argument` when the
child's mapped to-one field is not optional. They return `1` only when the
stored foreign key changed and `0` when it was already in the requested state.

`link` and `unlink` read only endpoint primary keys. They do not persist either
object, and all components of a simple or composite key must be present and
non-null. A model with a database-generated key must be selected after insert
before it is used as an endpoint. Missing endpoint rows are rejected by
foreign-key enforcement on both supported backends.

See [Collection relations](relations.md) for mapping declarations, generated
junction schemas, delete behavior, and self-referencing mappings.

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

PostgreSQL rejects full-model `GROUP BY`/`HAVING` queries before SQL execution,
because selecting arbitrary non-grouped model fields has no portable meaning.
Use `ProjectionQuery` and explicitly project grouped columns and aggregates.

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

Start a transaction with `beginTransaction`, then call `commitTransaction` or
`rollbackTransaction`:

```cpp
database.beginTransaction();

database.insert(objects);

database.commitTransaction();
```

or:

```cpp
database.beginTransaction();

database.insert(objects);

database.rollbackTransaction();
```

PostgreSQL marks a transaction failed after a statement error. `commitTransaction()`
then returns `DatabaseErrorCode::Transaction` without sending a misleading
commit; call `rollbackTransaction()` before starting another transaction.

Relation-table operations, `link`, `unlink`, and included selects participate
in the current explicit transaction and never start a private transaction.
Because an included select uses the parent query plus one or more batched
queries per included collection, wrap it in a transaction when those
statements must observe a single consistent application-level snapshot.
