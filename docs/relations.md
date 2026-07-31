# Collection relations

`orm-cxx` supports explicit one-level collection relations with
`orm::OneToMany<T>` and `orm::ManyToMany<T>`. Collection fields are model
metadata: they are not columns of the model's base table, are never written by
`insert`, and are loaded only when a query explicitly requests them. Mapping
descriptors use compile-time member pointers and `FixedString` names, and the
complete relation graph is validated by `orm::Schema<Models...>`.

1. [Collection wrappers](#collection-wrappers)
2. [One-to-many](#one-to-many)
3. [Many-to-many](#many-to-many)
4. [Junction column names](#junction-column-names)
5. [Schema lifecycle](#schema-lifecycle)
6. [Link and unlink](#link-and-unlink)
7. [Loading collections](#loading-collections)
8. [Filtering by collections](#filtering-by-collections)
9. [End-to-end workflow](#end-to-end-workflow)
10. [Transactions and consistency](#transactions-and-consistency)
11. [Validation and unsupported mappings](#validation-and-unsupported-mappings)
12. [Common mistakes](#common-mistakes)
13. [Migrating an existing schema](#migrating-an-existing-schema)

## Collection wrappers

A collection field uses one of the ORM wrappers rather than `std::vector<T>`:

```cpp
orm::OneToMany<Book> books;
orm::ManyToMany<Role> roles;
```

Both wrappers expose:

* `isLoaded()`
* `values()`
* `begin()` and `end()`
* `size()` and `empty()`
* `operator[]`

A default-constructed wrapper is empty and has `isLoaded() == false`. After an
explicit `include`, it has `isLoaded() == true`, including when the database
contains no related rows. This distinction lets callers tell "not requested"
from "requested and empty".

Changing `values()` or an element of the wrapper changes only the C++ object.
The ORM does not cascade-save or synchronize a collection. Use `link` and
`unlink` for relation mutations, and insert or update endpoint models
separately.

## One-to-many

`OneToMany` is the inverse of an existing to-one field on the child. Declare
the collection descriptor in the parent's static `relations` metadata and name
the child's to-one field with `mappedBy`:

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

`mappedBy` is required. The collection side is always selected by the typed
member pointer `&Author::books`. When the target type is complete, prefer
`.mappedBy<&Book::author>()`. The `FixedString` fallback
`.mappedBy<"author">()` is necessary in the common layout above because
`Book` is still incomplete when `Author::relations` is defined. In either form
the field must be a compatible to-one relation whose target is `Author`. The
`Book` table owns the foreign-key columns already generated for
`Book::author`; no hidden column or additional relation table is created for
`Author::books`.

Use `std::optional<Author>` for the child field when the relation must be
nullable:

```cpp
struct Book
{
    int id;
    std::string title;
    std::optional<Author> author;
};
```

This choice also determines whether `unlink(author, "books", book)` is valid.

## Many-to-many

The owning side declares the junction table with `through`:

```cpp
struct Role;

struct User
{
    int id;
    std::string name;
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
    std::string name;
    orm::ManyToMany<User> users;

    inline static constexpr auto relations =
        orm::relations(orm::manyToMany<&Role::users>().mappedBy<&User::roles>());
};
```

`through<"user_roles">()` is required on the owning side. The inverse side is
optional; when present, `mappedBy<&User::roles>()` points to the owning
collection field.
Both sides support `include`, collection predicates, `link`, and `unlink`.

Both endpoints and every other relation target must belong to the database's
closed schema:

```cpp
using AppSchema = orm::Schema<Author, Book, User, Role>;
orm::Database<AppSchema> database;
```

The junction table contains only the complete primary keys of both endpoints.
Every junction column is `NOT NULL`; all columns together form its composite
primary key. It has one foreign key to each endpoint with `ON DELETE CASCADE`.
Deleting an endpoint therefore deletes only its junction rows, never the model
at the other endpoint. No surrogate id, ordering column, or payload columns are
added.

## Junction column names

`ownerColumns` and `targetColumns` are optional. By default each junction
column is named `<endpoint-table>_<primary-key-column>`. Database column names,
including `columns_names` mappings, are used for the primary-key part.

For a composite primary key there must be exactly one junction column name per
primary-key column, in primary-key order:

```cpp
struct Tag;

struct Document
{
    std::string tenant;
    int id;
    orm::ManyToMany<Tag> tags;

    inline static constexpr auto id_columns =
        orm::primaryKey<&Document::tenant, &Document::id>();
    inline static constexpr auto relations =
        orm::relations(orm::manyToMany<&Document::tags>()
                           .through<"document_tags">()
                           .ownerColumns<"document_tenant_id", "document_id">()
                           .targetColumns<"tag_tenant_id", "tag_id">());
};
```

Self-referencing many-to-many mappings must specify distinct owner and target
column names explicitly, because the default names would collide.

## Schema lifecycle

Create endpoint tables first, then create junction tables from the owning
model:

```cpp
database.createTable<User>();
database.createTable<Role>();
database.createRelationTables<User>();
```

`createRelationTables<T>()` creates only owning many-to-many junction tables
declared by `T`. A one-to-many relation needs no relation table, and calling the
method for a model that has only inverse mappings is a no-op. Endpoint tables
must already exist before junction-table DDL can reference them.

Drop junction tables before endpoint tables:

```cpp
database.deleteRelationTables<User>();
database.deleteTable<Role>();
database.deleteTable<User>();
```

Relation-table creation and deletion are idempotent. They remain explicit;
`createTable<T>()` and `deleteTable<T>()` do not recursively alter relation
tables. Connecting to SQLite enables `PRAGMA foreign_keys=ON` automatically.

## Link and unlink

Use endpoint objects with complete, non-null primary-key values:

```cpp
User user{1, "Ada"};
Role admin{10, "admin"};

std::size_t inserted = database.link(user, "roles", admin);
std::size_t removed = database.unlink(user, "roles", admin);
```

For many-to-many, `link` inserts one junction row and `unlink` deletes it. Both
are idempotent: they return `1` when the stored relation changed and `0` when it
was already in the requested state. Foreign-key enforcement rejects missing
endpoint rows.

For one-to-many, pass the parent first and child last:

```cpp
std::size_t changed = database.link(author, "books", book);
std::size_t detached = database.unlink(author, "books", book);
```

`link` assigns the child's mapped foreign key and may move the child from a
different parent. `unlink` stores SQL `NULL` in that foreign key. It throws
`std::invalid_argument` when the mapped child relation is non-nullable. As with
many-to-many, the return value is `1` only when the stored foreign key changed
and `0` when it was already in the requested state.

These operations read primary keys only. They do not insert, update, or
cascade-save either endpoint. All primary-key components must be present and
non-null, including every component of a composite key. For an auto-increment
model, select the inserted row to obtain its generated key before calling
`link`.

## Loading collections

Collections are not joined into the main model query. Request one level
explicitly with `Query<T>::include`:

```cpp
orm::Query<Author> query;
query.include("books")
     .orderBy(orm::query::asc(orm::query::col("id")))
     .limit(20);

auto authors = database.select(query);
```

Duplicate includes are idempotent. The ORM first runs the unchanged parent
query, then one or more parameter-bounded batched queries per included field
and groups rows by the complete parent primary key. It never issues one
relation query per parent.

`LIMIT`, `OFFSET`, `DISTINCT`, and parent ordering apply only to the main query,
so each returned parent's collection is complete. Large primary-key sets are
split into batches that respect the selected backend's parameter limit. Element
order inside a collection is not guaranteed.

Includes are available only on full-model `Query<T>`, not on flat
`ProjectionQuery` results. Only one include level is supported: collection
fields on included elements remain unloaded. `disableJoining()` still controls
to-one hydration and is propagated to collection elements, but does not cancel
an explicit collection include.

## Filtering by collections

Collection predicates use correlated `EXISTS` subqueries:

```cpp
using namespace orm::query;

orm::Query<Author> prolific;
prolific.where(any("books", col("title").like("C++%")));

orm::Query<Author> withBooks;
withBooks.where(exists("books"));

orm::Query<Author> withoutDrafts;
withoutDrafts.where(none("books", col("title").like("Draft%")));
```

The predicate passed to `any` or `none` is relative to the collection's target
model. It may use scalar target fields and existing one-level to-one paths.
`any` renders correlated `EXISTS`, `none` renders `NOT EXISTS`, and `exists`
checks only whether the collection is non-empty. Values remain SOCI bind
parameters.

Filtering does not load the collection. Add `include("books")` separately when
the result objects also need the elements. Nested collection predicates are not
supported.

A collection is not a flat column path. Expressions such as
`col("books.title")` are rejected, as are collection fields in `ORDER BY`,
`GROUP BY`, projections, aggregate expressions, and `Update` assignments.

## End-to-end workflow

The complete lifecycle below uses both relation kinds. Endpoint rows are
inserted first, links are written explicitly, and collection values are loaded
only by `include`:

```cpp
using AppSchema = orm::Schema<Author, Book, User, Role>;
orm::Database<AppSchema> database;
database.connect("sqlite3://application.db");

// 1. Create base tables before the owning many-to-many junction table.
database.createTable<Author>();
database.createTable<Book>();
database.createTable<User>();
database.createTable<Role>();
database.createRelationTables<User>();

// 2. Store endpoints. Collection wrapper contents are ignored by insert().
Author author{1, "Octavia Butler"};
Book book{10, "Kindred", std::nullopt};
User user{1, "Ada"};
Role admin{10, "admin"};

database.insert(author);
database.insert(book);
database.insert(user);
database.insert(admin);

// 3. Persist associations explicitly.
database.link(author, "books", book); // updates Book::author
database.link(user, "roles", admin);  // inserts into user_roles

// The inverse many-to-many side is equivalent for mutations:
database.link(admin, "users", user);  // returns 0: the link already exists

// 4. Load collections explicitly.
orm::Query<Author> authorQuery;
authorQuery.include("books");
const auto authors = database.select(authorQuery);

if (!authors.empty() && authors[0].books.isLoaded())
{
    for (const Book& relatedBook : authors[0].books)
    {
        // use relatedBook
    }
}

// 5. Detach links before removing rows when application rules require it.
database.unlink(author, "books", book);
database.unlink(user, "roles", admin);

// 6. Drop owning junction tables before endpoint tables.
database.deleteRelationTables<User>();
database.deleteTable<Role>();
database.deleteTable<User>();
database.deleteTable<Book>();
database.deleteTable<Author>();
```

For generated primary keys, insert the endpoint and select it back before step
3. Calling `link` with the original object whose auto-increment key is still
empty is rejected.

A complete program containing the model declarations and this lifecycle is
available in [`examples/relations.cpp`](../examples/relations.cpp). It is built
as the `relations-example` CMake target.

## Transactions and consistency

Schema operations, `link`, `unlink`, and collection queries participate in the
database's current explicit transaction. They do not start or commit a private
transaction.

An included select uses multiple SQL statements. It does not create an
automatic snapshot around them; wrap the select in an explicit transaction
when the parent rows and included collections must be observed consistently.

Deleting a one-to-many parent is blocked by the child's foreign key unless the
application detaches or removes the children first. The ORM does not generate
cascade deletion for child rows.

## Validation and unsupported mappings

Instantiating `orm::Schema<Models...>` rejects invalid collection mappings at
compile time, including:

* a wrapper without exactly one matching descriptor,
* a missing or incompatible `mappedBy` field,
* an owning many-to-many relation without `through`,
* a target model without a real, non-empty primary key,
* `optional<OneToMany<T>>` or `optional<ManyToMany<T>>`,
* conflicting junction-table declarations,
* junction column counts that differ from endpoint primary-key counts,
* colliding owner and target columns in a self-reference.

Collection mapping cycles are supported because relation targets are resolved
lazily from cached metadata rather than recursively copied.

Lazy loading, nested includes, nested collection predicates, automatic
collection synchronization, cascade-save, ordered collections, junction
payload models, and schema migrations are outside the current contract.

## Common mistakes

* `oneToMany<&Author::books>()` and `manyToMany<&User::roles>()` require
  collection member pointers. Prefer a target member pointer for `mappedBy`;
  its `FixedString` fallback names a C++ field, not a SQL column.
* Runtime query and mutation calls such as `include("roles")`,
  `exists("roles")`, and `link(user, "roles", role)` still use reflected C++
  field names. Compile-time query-path validation is planned separately.
* Adding an element with `model.roles.values().push_back(role)` changes only
  that in-memory wrapper. Call `database.link(model, "roles", role)` to persist
  the association.
* `insert(model)` never inspects or stores collection wrapper contents. Insert
  both endpoints first and then call `link`.
* An empty wrapper with `isLoaded() == false` does not prove that the database
  relation is empty. Use `include` and check for `isLoaded() == true` before
  interpreting `empty()` as a database result.
* `include("roles")` and `where(exists("roles"))` are independent. The first
  hydrates the wrapper; the second filters parent rows.
* Only the owning many-to-many model calls `createRelationTables<T>()` and
  `deleteRelationTables<T>()`. Calling them for the inverse model is a no-op.
* Do not call `deleteTable` for an endpoint while its owning junction table
  still exists. Drop the junction table first.

## Migrating an existing schema

Adding relation metadata does not migrate an existing database. Apply schema
changes manually:

* for one-to-many, add or validate the child's mapped foreign-key columns and
  nullability;
* for many-to-many, create the junction table with the same endpoint columns,
  composite primary key, and foreign keys generated by the ORM;
* create junction tables only after both endpoint tables exist;
* remove junction tables before dropping endpoint tables.

Test migration SQL on a copy of the database before enabling the new mapping in
an application release.
