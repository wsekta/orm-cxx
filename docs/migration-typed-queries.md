# Migrating to typed queries in 0.2

Version `0.2.0` removes the 0.1 string-field query API. Query expressions now
carry their root model, scalar type, nullability, and write permissions in C++
types. Convert the model mappings and closed schema first using
[Migrating to static schemas](migration-static-schema.md).

## Replace 0.1 expressions

The left column contains the removed API; use the right column in 0.2:

| Before (0.1) | After (0.2) |
| --- | --- |
| `col("age")` or `field("age")` | `col<&User::age>()` |
| `col("profile.city")` | `col<&User::profile, &Profile::city>()` |
| `asc(col("age"))`, `desc(col("age"))` | `asc(col<&User::age>())`, `desc(col<&User::age>())` |
| `sum(col("age"))`, `avg(col("age"))` | `sum(col<&User::age>())`, `avg(col<&User::age>())` |
| `countAll()` | `countAll<User>()` |
| `any("books", predicate)` | `any<&Author::books>(predicate)` |
| `none("books", predicate)` | `none<&Author::books>(predicate)` |
| `exists("books")` | `exists<&Author::books>()` |
| `query.include("books")` | `query.include<&Author::books>()` |
| `database.link(user, "roles", role)` | `database.link<&User::roles>(user, role)` |
| `database.unlink(user, "roles", role)` | `database.unlink<&User::roles>(user, role)` |
| `update.set(col("email"), value)` | `update.set(col<&User::email>(), value)` |
| `raw(sql, params...)` | `raw<User>(sql, params...)` |
| `rawOrder(sql)` | `rawOrder<User>(sql)` |

DTO aliases passed to `as("dtoField", expression)`, raw SQL text, parameter
names, and compile-time physical column mappings retain their current syntax.
Source member pointers name C++ members rather than physical database columns.

## Keep expression ownership consistent

```cpp
struct Profile
{
    int id;
    std::string city;
};

struct User
{
    int id;
    int age;
    double score;
    std::optional<std::string> email;
    std::optional<Profile> profile;
};

using AppSchema = orm::Schema<User, Profile>;
orm::Database<AppSchema> database;

using namespace orm::query;
orm::Query<User> query;
query.where(col<&User::age>() >= short{18})
     .andWhere(col<&User::profile, &Profile::city>() == "Warsaw")
     .orderBy(asc(col<&User::id>()));
```

All predicates, ordering, grouping, HAVING, projection expressions, and update
assignments must have the builder's root model. Collection predicates accept
only predicates rooted in their target model and return a predicate rooted in
their owner. A direct column or a one-level to-one path is supported; collection
columns, nested collection predicates, and deeper paths fail to compile.

## Use values that preserve their entire type range

Comparison, IN, BETWEEN, HAVING, and update values allow compatible scalars
and numeric widening that preserves the source type's entire range and precision:

```cpp
query.where(col<&User::age>() >= short{18});

orm::Update<User> update;
update.set(col<&User::score>(), 0.5F)
      .set(col<&User::email>(), std::optional<std::string>{"new@example.com"})
      .where(col<&User::id>() == 1);
```

The decision uses the value's C++ type, regardless of its current value.
Narrowing, signed-to-unsigned changes, floating-point-to-integer conversions,
and integer-to-floating conversions with insufficient precision are rejected.
An unsigned value can widen to a signed type only if that signed type covers
its whole range. Boolean values are separate from numeric values. String
fields accept literals, `std::string`, and `std::string_view`; LIKE requires a
string field. Explicit casts are the caller's responsibility and should follow
an application-level range check when narrowing is intentional.

## Make NULL and writes explicit

NULL predicates require an optional scalar or a path through an optional to-one
relation. Filter values cannot be `std::optional<T>`; unwrap a present value or
use `isNull()`, `isNotNull()`, `nullptr`, or `std::nullopt` explicitly:

```cpp
query.where(col<&User::email>().isNull());
query.where(col<&User::profile, &Profile::id>() == nullptr);

update.set(col<&User::email>(), std::nullopt)
      .where(col<&User::profile, &Profile::id>() == 1);
```

Optional update values are accepted only for nullable targets and when their
contained type satisfies the scalar value rule. Write predicates and assignments
support direct fields and related primary-key paths mapped to local foreign keys.
Related non-primary-key paths fail to compile because writes do not generate joins.

## Retain runtime validation at the database boundary

DTO alias names and completeness, empty IN lists, required update predicates
and assignments, raw parameter names, disabled joins, backend capabilities,
and database errors remain runtime checks. Raw SQL is explicitly rooted with
`raw<Model>` and `rawOrder<Model>`; C++ cannot validate its SQL contents.

The typed API does not change table or column names, generated SQL semantics,
bind parameter behavior, or the result type of full-model selects. Run existing
SQLite and PostgreSQL tests after migration; no database schema migration is
needed when the physical model mappings stay the same.
