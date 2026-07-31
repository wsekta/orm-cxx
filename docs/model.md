# Model

1. [Create a model](#create-a-model)
2. [Static schema](#static-schema)
3. [Supported field types](#supported-field-types)
4. [Optional fields](#optional-fields)
5. [Table name](#table-name)
6. [Column names](#column-names)
7. [Primary key](#primary-key)
8. [Auto-increment primary key](#auto-increment-primary-key)
9. [One-to-one relations](#one-to-one-relations)
10. [Collection relations](#collection-relations)
11. [Current limitations](#current-limitations)

## Create a model

A model is a reflected C++ struct whose fields can be mapped to database columns.

```cpp
struct User {
    int id;
    std::string name;
    std::string email;
};
```

Models must be C++ aggregates. Their field names and types are reflected during
compilation; unions, raw C-array fields, bit-fields, and non-aggregate classes
are rejected. Inherited aggregates are outside the supported contract and must
use composition instead, even if a compiler happens to accept a particular
structured binding. By default the table name is derived from the C++ type name
and `::` is replaced with `_`.

## Static schema

Every database is bound to a closed set of complete model types:

```cpp
using AppSchema = orm::Schema<User>;
orm::Database<AppSchema> database;
```

The schema validates each model and the complete relation graph at compile
time. A model operation fails to compile when its type is absent from the
database's schema. Include every to-one and collection relation target in the
same schema, even when only one endpoint is queried directly.

## Supported field types

The bundled SQLite and PostgreSQL backends support these scalar C++ field types:

* `bool`
* `char`, `signed char`, `unsigned char`
* `short`, `short int`, `unsigned short`, `short unsigned int`
* `int`
* `long`, `long int`, `unsigned int`, `unsigned long`, `long unsigned int`
* `long long`, `long long int`, `__int64`
* `unsigned long long`, `long long unsigned int`, `unsigned __int64`
* `float`
* `double`
* `std::string`

Each backend maps these types to its native SQL types. SQLite uses its integer,
real, and text affinities. PostgreSQL uses `BOOLEAN`, width-appropriate
`SMALLINT`/`INTEGER`/`BIGINT`, `DOUBLE PRECISION`, and `TEXT`. See the
[backend matrix](backends.md) for backend-specific limits and semantics. Types
outside this list are not supported as scalar columns.

Both bundled SOCI backends exchange the widest integer values through signed
64-bit storage. Consequently, `unsigned long long` and 64-bit `unsigned long`
values are supported through `INT64_MAX`; larger values fail with
`DatabaseErrorCode::Conversion` instead of being silently wrapped. The selected
backend reports this bound in `BackendCapabilities::valueLimits`.

## Optional fields

Wrap a supported scalar type in `std::optional<T>` to make the column nullable.

```cpp
struct User {
    int id;
    std::optional<std::string> email;
};
```

Non-optional fields are generated as `NOT NULL`. Optional fields are generated
without `NOT NULL`, and `std::nullopt` is stored and read back as SQL `NULL`.

## Table name

Override the generated table name with a static `table_name` field:

```cpp
struct User {
    int id;
    std::string name;

    inline static constexpr orm::reflection::FixedString table_name{"users"};
};
```

`table_name` must be a structural `FixedString`; runtime `std::string` and
`std::string_view` metadata are no longer accepted.

## Column names

Override database column names with `orm::columnNames` and typed member
pointers. Fields omitted from the mapping keep their reflected C++ names.

```cpp
struct User {
    int id;
    std::string displayName;

    inline static constexpr auto columns_names =
        orm::columnNames(orm::columnName<&User::id, "user_id">(),
                         orm::columnName<&User::displayName, "display_name">());
};
```

Query and update builders still use C++ field names such as `col("displayName")`;
the renderer maps them to database column names.

Physical non-identity column names are also used as prepared-statement bind
names by the bundled adapters. Keep those mapped names to ASCII letters, digits,
and `_`; unsupported names are rejected during model validation rather than
being passed to a database driver. Table names and all identifiers must be
non-empty and cannot contain an embedded NUL byte. PostgreSQL additionally
enforces its 63-byte identifier limit.

## Primary key

If a model has an `id` field, it is used as the default primary key.

```cpp
struct User {
    int id;
    std::string name;
};
```

Override the primary key with `orm::primaryKey` and member pointers. The order
of the pointers is the order of a composite key.

```cpp
struct User {
    int id;
    int tenantId;
    std::string name;

    inline static constexpr auto id_columns =
        orm::primaryKey<&User::tenantId, &User::name>();
};
```

To create a model without a primary key, omit `id` or define an empty
`primaryKey` definition.

```cpp
struct LogEntry {
    std::string message;

    inline static constexpr auto id_columns = orm::primaryKey<>();
};
```

## Auto-increment primary key

Integer primary keys can be generated by either bundled database backend. To
enable this, define `auto_increment_columns` with `orm::autoIncrement`:

```cpp
struct User {
    int id;
    std::string name;

    inline static constexpr auto auto_increment_columns =
        orm::autoIncrement<&User::id>();
};
```

SQLite creates `id INTEGER PRIMARY KEY AUTOINCREMENT`; PostgreSQL creates
`id INTEGER GENERATED BY DEFAULT AS IDENTITY PRIMARY KEY`. Both omit the field
from generated `INSERT` statements. Existing models are unchanged unless they
define `auto_increment_columns`.

Auto-increment support has these limitations:

* The auto-increment column must be the only primary-key column.
* The column must have type `int`.
* The column cannot be `std::optional`.
* The column cannot be a related model field.

Column mappings and auto-increment declarations refer to the same C++ member
independently:

```cpp
struct User {
    int id;
    std::string name;

    inline static constexpr auto auto_increment_columns =
        orm::autoIncrement<&User::id>();
    inline static constexpr auto columns_names =
        orm::columnNames(orm::columnName<&User::id, "user_id">());
};
```

This applies the mapped `user_id` name to the backend's corresponding
auto-generated integer primary-key declaration.

## One-to-one relations

A field whose type is another model with a primary key is treated as a one-to-one
relation. The owning table stores the related model primary key as local foreign
key column(s).

```cpp
struct Profile {
    int id;
    std::string city;
};

struct User {
    int id;
    Profile profile;
};
```

For this model, `User` stores `profile_id` and references `Profile(id)`.
Selecting a model joins one-to-one relations by default. Use
`Query<T>::disableJoining()` to read only related primary-key values.

Relations can also be nullable:

```cpp
struct User {
    int id;
    std::optional<Profile> profile;
};
```

Nullable relations generate nullable foreign-key columns, store `std::nullopt`
as SQL `NULL`, and read SQL `NULL` back as `std::nullopt`.

These fields are represented by separate to-one relation metadata rather than
by a scalar `ColumnType`. The ORM does not add a `UNIQUE` constraint to their
foreign-key columns.

## Collection relations

Use `orm::OneToMany<T>` and `orm::ManyToMany<T>` for fields that contain
multiple related models. A collection field must have exactly one matching
entry in the model's static `relations` metadata:

```cpp
struct Book;

struct Author
{
    int id;
    std::string name;
    orm::OneToMany<Book> books;

    inline static constexpr auto relations =
        orm::relations(orm::oneToMany<&Author::books>().mappedBy<"author">());
};

struct Book
{
    int id;
    std::string title;
    Author author;
};
```

The collection itself is selected with the typed member pointer
`&Author::books`. `mappedBy` names the existing to-one field on the child.
When the target type is complete, use the typed form
`.mappedBy<&Book::author>()`. The `FixedString` form shown above is the
supported fallback for this common forward-declaration cycle. `OneToMany` does
not add a column to the parent or create a hidden child foreign key.

The owning side of a many-to-many relation declares its junction table:

```cpp
struct Role;

struct User
{
    int id;
    orm::ManyToMany<Role> roles;

    inline static constexpr auto relations =
        orm::relations(orm::manyToMany<&User::roles>()
                           .through<"user_roles">()
                           .ownerColumns<"user_id">()
                           .targetColumns<"role_id">());
};

struct Role
{
    int id;
    orm::ManyToMany<User> users;

    inline static constexpr auto relations =
        orm::relations(orm::manyToMany<&Role::users>().mappedBy<&User::roles>());
};
```

The inverse `mappedBy` side is optional. Endpoint models must have non-empty
primary keys. `optional<OneToMany<T>>` and `optional<ManyToMany<T>>` are not
valid mappings; the wrapper already represents an unloaded or loaded-empty
collection.

Declare the complete relation graph in one schema:

```cpp
using AppSchema = orm::Schema<Author, Book, User, Role>;
orm::Database<AppSchema> database;
```

Collection fields are omitted from base-table DDL, insert statements, and
ordinary row binding. Inserting a model never traverses or saves a collection.
See [Collection relations](relations.md) for wrapper behavior, junction naming,
schema lifecycle, explicit mutations, includes, predicates, composite keys,
and validation rules.

## Current limitations

Model metadata supports to-one fields and explicitly mapped `OneToMany` and
`ManyToMany` collection fields. Mapping definitions are compile-time values;
the old runtime `string`/`string_view`, `vector`, and `map` metadata forms are
not accepted. It does not support nested collection loading, junction models
with payload fields, ordered relations, custom converters, date/time fields,
UUID fields, or `boost::optional`. See
[Migrating to static schemas](migration-static-schema.md) for the breaking API
changes.
