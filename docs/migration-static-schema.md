# Migrating to compile-time mappings and static schemas

The model API now treats metadata as part of the C++ type system. This is a
breaking change: runtime `string`, `string_view`, `vector`, and `map`
configuration objects are replaced by structural strings, member pointers, and
a closed schema.

## Migration summary

| Earlier declaration | Static declaration |
| --- | --- |
| `std::string_view table_name = "users"` | `orm::reflection::FixedString table_name{"users"}` |
| `std::map columns_names{{"displayName", "display_name"}}` | `orm::columnNames(orm::columnName<&User::displayName, "display_name">())` |
| `std::vector id_columns{"tenant", "id"}` | `orm::primaryKey<&User::tenant, &User::id>()` |
| empty `id_columns` vector | `orm::primaryKey<>()` |
| `std::vector auto_increment_columns{"id"}` | `orm::autoIncrement<&User::id>()` |
| `oneToMany("books")` | `oneToMany<&Author::books>()` |
| `manyToMany("roles").through("user_roles")` | `manyToMany<&User::roles>().through<"user_roles">()` |
| inverse `mappedBy("roles")` | `mappedBy<&User::roles>()` |
| `orm::Database database` | `orm::Database<orm::Schema<Models...>> database` |

The static member names (`table_name`, `columns_names`, `id_columns`,
`auto_increment_columns`, and `relations`) are retained. Their value types are
what changed.

## 1. Keep models reflectable

A model must be an aggregate with public data fields. The local C++20
reflection layer supports empty aggregates and aggregates with up to 128
fields. Non-aggregates, unions, raw C-array fields, and bit-fields produce
compile-time diagnostics. Inherited aggregates are outside the supported
contract: common forms cannot be decomposed and fail at compile time, while a
small subset can be accepted by the C++ structured-binding rules and must not
be relied on. Use composition instead. Use `std::array<T, N>` instead of a raw
array field.

No `reflect-cpp` headers or library target are required by consumers. The
reflection mechanics used by `orm-cxx` are provided by its public headers.

`orm::reflection::typeName<T>()`, `memberName<&T::field>()`, and
`valueName<Value>()` return exact-size `FixedString` values during constant
evaluation. `valueName` supports enumerators and symbols or addresses that are
valid non-type template parameters. C++20 does not expose the source spelling
of an ordinary local variable; recovering that spelling would require a macro
and is intentionally outside this API.

## 2. Replace runtime model configuration

Before:

```cpp
struct User
{
    int id;
    std::string displayName;

    inline static constexpr std::string_view table_name = "users";
    inline static const std::map<std::string, std::string> columns_names = {
        {"id", "user_id"},
        {"displayName", "display_name"},
    };
    inline static const std::vector<std::string> id_columns = {"id"};
    inline static const std::vector<std::string> auto_increment_columns = {"id"};
};
```

After:

```cpp
struct User
{
    int id;
    std::string displayName;

    inline static constexpr orm::reflection::FixedString table_name{"users"};
    inline static constexpr auto columns_names =
        orm::columnNames(orm::columnName<&User::id, "user_id">(),
                         orm::columnName<&User::displayName, "display_name">());
    inline static constexpr auto id_columns =
        orm::primaryKey<&User::id>();
    inline static constexpr auto auto_increment_columns =
        orm::autoIncrement<&User::id>();
};
```

Put a mapping after the member declarations it references. A mistyped field
can no longer silently survive until model construction: an invalid member
pointer, duplicate member, empty name, incompatible primary key, or invalid
auto-increment definition produces a compile-time diagnostic.

An `int id` field remains the default primary key when `id_columns` is absent.
Use `orm::primaryKey<>()` when a model is intentionally keyless.

## 3. Convert collection mappings

The collection field is always identified by a member pointer. Builder strings
are non-type template parameters:

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

For one-to-many mappings, prefer a target member pointer when the target type
is complete:

```cpp
orm::oneToMany<&Author::books>().mappedBy<&Book::author>()
```

The normal `Author`/`Book` declaration order forms a C++ completeness cycle.
For that case only, use the structural-name fallback:

```cpp
orm::oneToMany<&Author::books>().mappedBy<"author">()
```

Runtime operations have not yet moved to member pointers. Calls such as
`query.include("roles")`, `exists("roles")`, and
`database.link(user, "roles", role)` still take reflected C++ field names.

## 4. Define the closed schema

List every model the database may operate on, including all to-one and
collection targets:

```cpp
using AppSchema = orm::Schema<User, Role, Author, Book>;

orm::Database<AppSchema> database;
database.connect("sqlite3://application.db");
```

The schema validates unique model types and table names, relation targets,
inverse mappings, primary keys, logical and generated foreign-key column
names, junction tables, and junction column counts. Calling a model operation
with a type outside `AppSchema` fails to compile.

Typed code can inspect `modelDescriptor<AppSchema, T>()` during constant
evaluation. Runtime backend extensions receive `modelView<AppSchema, T>()`, a
trivially-copyable two-word handle containing the schema address and model
index. The handle exposes immutable descriptor data through `operator->` and
resolves relation targets by schema index, so cyclic schemas do not allocate
or recursively copy metadata:

```cpp
constexpr auto user = orm::modelView<AppSchema, User>();
static_assert(user->tableName == "users");
static_assert(user->columns[0].isPrimaryKey);
```

A backend connection probe that performs no model operations can use an empty
schema:

```cpp
using AppSchema = orm::Schema<>;
orm::Database<AppSchema> database;
```

## 5. Keep query paths unchanged for now

This migration does not rename the current query DSL. `col("displayName")`
still uses the reflected C++ field name and is translated through
`columns_names`; it does not use the physical `display_name` spelling.
One-level paths such as `col("profile.city")` and collection names passed to
`include`, `any`, `exists`, and `none` remain runtime strings.

Compile-time query-field and relation-path validation is the next focused API
step. Until then, mapping and schema errors are compile-time failures while
query-path and backend-capability errors are reported when a query is
validated or rendered.

## Suggested rollout

1. Convert each model's table, column, primary-key, and auto-increment metadata.
2. Convert relation descriptors to member-pointer builders.
3. Create one `orm::Schema` containing the complete relation graph.
4. Change every database declaration to `orm::Database<SchemaType>`.
5. Compile before changing database files; compile-time failures identify
   incomplete schemas and incompatible mappings.
6. Run existing backend tests. The SQL schema is unchanged when the new
   definitions encode the same physical names and keys.

Static metadata does not migrate an existing database. Continue to apply SQL
schema changes explicitly and test migrations against a copy of production
data.
