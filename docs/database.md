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

Create a database connection independently of model schemas. Obtain a borrowed
ORM context for each closed compile-time schema:

```cpp
struct User
{
    int id;
};

using AppSchema = orm::Schema<User>;
orm::Database database;
auto context = database.orm<AppSchema>();
database.connect("sqlite3://test.db");
```

Every model used by `select`, `insert`, schema operations, or relation
operations must belong to `AppSchema`. All relation targets must be listed as
well. Missing models and invalid mappings fail during compilation.

`Database` owns the connection and transaction. `OrmContext<Schema>` borrows
that database; it does not own a connection or cache a backend. Creating or
copying a context requires no allocation. A context can be created before
connecting, copied, moved, or assigned to another database's context of the same
schema. Model operations require a connected database.

The database must outlive every context that refers to it. Contexts cannot be
default constructed or obtained from a temporary or const database. Destroying
a context leaves the connection and transaction open. Disconnecting rolls back
the transaction; existing contexts work again after reconnecting, using the
current backend. The session is shared, so contexts do not enable concurrent
access to a database.

`orm::Schema<...>` describes a closed set of C++ models and their relations. It
does not select a PostgreSQL namespace or verify the physical database layout.

The remaining fragments are independent examples. In each case, the
`context` object's schema must contain every model named by that operation.

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
same `Database` object can then connect again; existing ORM contexts use its new
session and backend. A `Database` is intentionally
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

To create a table in the database, use `createTable` method and pass model as template argument:

```cpp
struct ObjectModel
{
    int id;
    std::string name;
    std::optional<std::string> email;
    std::string password;
    std::string created_at;
    std::string updated_at;
};

context.createTable<ObjectModel>();
```

## Delete table

To delete a table from the database, use `deleteTable` method and pass model as template argument:

```cpp
context.deleteTable<ObjectModel>();
```

## Create and delete relation tables

Base model tables and many-to-many junction tables have an explicit lifecycle.
Create both endpoint tables first, then create junction tables from the owning
model:

```cpp
context.createTable<User>();
context.createTable<Role>();
context.createRelationTables<User>();
```

`createRelationTables<T>()` creates only junction tables owned by `T`. It is a
no-op for one-to-many mappings and inverse many-to-many mappings. Repeated calls
are safe. Endpoint tables must already exist.

Drop junction tables before either endpoint table:

```cpp
context.deleteRelationTables<User>();
context.deleteTable<Role>();
context.deleteTable<User>();
```

`deleteRelationTables<T>()` is also idempotent and affects only junction tables
owned by `T`. `createTable`, `deleteTable`, `insert`, and row deletion never
recursively create, drop, or synchronize relation tables.

## Insert objects

To insert objects into the database, use `insert` method and pass vector of objects as argument:

```cpp
std::vector<ObjectModel> objects{
    {1, "name", "email", "password", "created_at", "updated_at"}, 
    {2, "name2", "email2", "password2", "created_at2", "updated_at2"}
};

context.insert(objects);
```

You can also insert single object:

```cpp
ObjectModel object{1, "name", "email", "password", "created_at", "updated_at"};

context.insert(object);
```

For models with an auto-increment primary key, the generated `INSERT` statement omits that primary-key column and
the selected database assigns the value:

```cpp
struct User
{
    int id;
    std::string name;

    inline static constexpr auto auto_increment_columns =
        orm::autoIncrement<&User::id>();
};

context.createTable<User>();
context.insert(User{0, "Ann"});
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

context.insert(User{1, std::nullopt});
```

`OneToMany` and `ManyToMany` wrapper fields are ignored by `insert`. Endpoint
objects must be inserted separately, followed by explicit `link` calls where a
stored relation is needed. There is no cascade-save.

## Link and unlink relations

Many-to-many mutations add or remove one junction row:

```cpp
User user{1, "Ada"};
Role admin{10, "admin"};

std::size_t linked = context.link<&User::roles>(user, admin);
std::size_t unlinked = context.unlink<&User::roles>(user, admin);
```

Each operation is idempotent: it returns `1` when the relation changed and `0`
when the database was already in the requested state. The same operations can
be called through an inverse many-to-many field.

For one-to-many, select the collection member in the template argument and pass the parent and child:

```cpp
std::size_t assigned = context.link<&Author::books>(author, book);
std::size_t detached = context.unlink<&Author::books>(author, book);
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
query.where(col<&ObjectModel::name>().like("name%"))
     .orderBy(asc(col<&ObjectModel::id>()))
     .limit(10);

auto queriedObjects = context.select(query);
```

PostgreSQL rejects full-model `GROUP BY`/`HAVING` queries before SQL execution,
because selecting arbitrary non-grouped model fields has no portable meaning.
Use `ProjectionQuery` and explicitly project grouped columns and aggregates.

## Update objects

To update rows, build an `orm::Update<Model>` with one or more assignments and a required predicate:

```cpp
using namespace orm::query;

orm::Update<ObjectModel> update;
update.set(col<&ObjectModel::email>(), "new-email@example.com")
      .set(col<&ObjectModel::updated_at>(), "updated_at")
      .where(col<&ObjectModel::id>() == 1);

std::size_t updatedRows = context.update(update);
```

Use `std::nullopt` to store `NULL` in nullable columns:

```cpp
orm::Update<ObjectModel> clearEmail;
clearEmail.set(col<&ObjectModel::email>(), std::nullopt)
          .where(col<&ObjectModel::id>() == 1);
```

`update` returns the number of affected rows. Calling it without a `where` predicate, or without assignments throws
`std::invalid_argument`. Assigning `NULL` or an optional value to a non-nullable column fails to compile.
Assignments accept only type-compatible values with safe numeric widening.
Nullable assignments also accept `std::optional<T>` when `T` satisfies the same
value rule; an empty optional stores SQL `NULL`. Filter values do not accept
optionals: test nullability explicitly in the predicate.

## Remove objects

To delete rows, call `remove` with the model type and a required predicate:

```cpp
using namespace orm::query;

std::size_t removedRows = context.remove<ObjectModel>(col<&ObjectModel::id>() == 1);
```

`remove` returns the number of affected rows. There is no unfiltered public delete-row API.

## Transactions

Start a transaction with `beginTransaction`, then call `commitTransaction` or
`rollbackTransaction` on the database. All contexts obtained from that database
participate in the same transaction:

```cpp
struct AuditEntry
{
    int id;
    std::string action;
};

using AuditSchema = orm::Schema<AuditEntry>;
auto audit = database.orm<AuditSchema>();
context.createTable<User>();
audit.createTable<AuditEntry>();

database.beginTransaction();
context.insert(User{1});
audit.insert(AuditEntry{1, "created user"});
database.commitTransaction();
```

For a single context:

```cpp
database.beginTransaction();

context.insert(objects);

database.commitTransaction();
```

or:

```cpp
database.beginTransaction();

context.insert(objects);

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
