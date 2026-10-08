# Query

The mutable builder is described below. For reusable query shapes and typed
argument slots, see [static query plans](static-queries.md). Expressions also
offer `.dynamic()` when a named facade is needed; see the
[0.3 migration guide](migration-static-queries.md).

1. [Build select](#build-select)
2. [Loading collections](#loading-collections)
3. [Where predicates](#where-predicates)
4. [Collection predicates](#collection-predicates)
5. [Column names and relations](#column-names-and-relations)
6. [Ordering](#ordering)
7. [Distinct](#distinct)
8. [Limit and offset](#limit-and-offset)
9. [Raw SQL fragments](#raw-sql-fragments)
10. [Partial-result queries](#partial-result-queries)
11. [Full-model grouping and HAVING](#full-model-grouping-and-having)
12. [Aggregate projection queries](#aggregate-projection-queries)
13. [Write predicates](#write-predicates)
14. [Current limitations](#limitations)

## Build select

To query objects from a table use `orm::Query<Model>` and pass it to `orm::Database::select`.
The result is always `std::vector<Model>`.

```cpp
#include "orm-cxx/orm.hpp"

struct Profile
{
    int id;
    std::string city;
};

struct User
{
    int id;
    std::string name;
    std::optional<std::string> email;
    int age;
    bool active;
    std::optional<Profile> profile;

    inline static constexpr orm::reflection::FixedString table_name{"users"};
};

using AppSchema = orm::Schema<User, Profile>;
orm::Database<AppSchema> database;
database.connect("sqlite3://test.db");

orm::Query<User> query;
auto users = database.select(query);
```

The fragments below use these models unless a section declares its own
independent example. Collection examples use the `Author` and `Book` mappings
from [Collection relations](relations.md).

`Query` is a builder, so methods can be chained:

```cpp
using namespace orm::query;

orm::Query<User> query;
query.where(col<&User::age>() >= 18)
     .orderBy(desc(col<&User::id>()))
     .limit(10)
     .offset(20);
```

## Loading collections

`OneToMany` and `ManyToMany` fields are unloaded by default. Request a
collection explicitly on a full-model query:

```cpp
orm::Query<Author> query;
query.include<&Author::books>()
     .orderBy(orm::query::asc(orm::query::col<&Author::id>()))
     .limit(20);

std::vector<Author> authors = database.select(query);
```

After selection, every returned `books` wrapper has `isLoaded() == true`, even
when it is empty. Collection wrappers that were not included remain empty with
`isLoaded() == false`. Repeating the same include is idempotent.

The main query is executed first, followed by one or more parameter-bounded
batched queries for each unique included field. Results are grouped by the
complete parent primary key, so parents are not duplicated and the
implementation never issues one query per parent. Large key sets are split to
respect the selected backend's parameter limit.

Pagination, `DISTINCT`, and ordering apply to the parent query only. Included
collections are complete for the selected parents, but their element order is
not guaranteed. Includes support one level; collection wrappers within loaded
elements stay unloaded.

`disableJoining()` controls existing to-one joins and is propagated while
hydrating collection elements. It does not disable an explicit include.
`ProjectionQuery` has no `include` method because its result is a flat DTO.

## Where predicates

Use helpers from `orm::query` to build a predicate tree.

```cpp
using namespace orm::query;

orm::Query<User> query;
query.where((col<&User::age>() >= 18 && col<&User::name>().like("Ann%")) || col<&User::email>().isNull());
```

Supported operators:

| C++ API | SQL |
| --- | --- |
| `col<&User::age>() == 18` | `=` |
| `col<&User::age>() != 18` | `!=` |
| `col<&User::age>() > 18` | `>` |
| `col<&User::age>() >= 18` | `>=` |
| `col<&User::age>() < 18` | `<` |
| `col<&User::age>() <= 18` | `<=` |
| `col<&User::name>().like("Ann%")` | `LIKE` |
| `col<&User::name>().notLike("Ann%")` | `NOT LIKE` |
| `col<&User::email>().isNull()` or `col<&User::email>() == nullptr` | `IS NULL` |
| `col<&User::email>().isNotNull()` or `col<&User::email>() != nullptr` | `IS NOT NULL` |
| `col<&User::age>().in({18, 19, 20})` | `IN` |
| `col<&User::age>().notIn({18, 19, 20})` | `NOT IN` |
| `col<&User::age>().between(18, 30)` | `BETWEEN` |
| `col<&User::age>().notBetween(18, 30)` | `NOT BETWEEN` |

Logical operators are available with `&&`, `||` and `!`.

```cpp
query.where(!(col<&User::name>().like("test%") || col<&User::email>().isNull()));
```

Calling `where` replaces the current predicate. Use `andWhere` or `orWhere` to append to the current predicate:

```cpp
query.where(col<&User::age>() >= 18)
     .andWhere(col<&User::email>().isNotNull())
     .orWhere(col<&User::name>() == "admin");
```

All comparison values are sent to the database as SOCI bind parameters. They are not interpolated into SQL strings.

## Collection predicates

Use `any`, `exists`, and `none` to filter a model by a mapped collection:

```cpp
using namespace orm::query;

orm::Query<Author> query;
query.where(any<&Author::books>(col<&Book::title>().like("C++%")) &&
            none<&Author::books>(col<&Book::title>().like("Draft%")));

orm::Query<Author> nonEmpty;
nonEmpty.where(exists<&Author::books>());
```

`any<&Author::books>(predicate)` renders a correlated `EXISTS` subquery.
`none<&Author::books>(predicate)` renders `NOT EXISTS`, and `exists<&Author::books>()` checks
only whether at least one related row exists. They work for both `OneToMany`
and `ManyToMany`, including inverse many-to-many mappings.

The element predicate is resolved relative to the target model. It may contain
target scalar fields and that model's existing one-level to-one paths. Values
are bound normally; they are never interpolated into SQL. Nested collection
predicates are rejected at compile time.

A collection predicate filters only. It does not mark the wrapper loaded or
fetch its elements; combine it with `include<&Author::books>()` when both behaviors are
needed.

## Column names and relations

`col<&User::field>()` references the C++ model member. If the model defines `columns_names`, the query renderer maps the field name to the configured database column name.

```cpp
struct User
{
    int id;
    std::string displayName;

    inline static constexpr auto columns_names =
        orm::columnNames(
            orm::columnName<&User::displayName, "display_name">());
};

orm::Query<User> query;
query.where(orm::query::col<&User::displayName>() == "Wojtek");
```

For one-to-one related models use a one-level path:

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

orm::Query<User> query;
query.where(orm::query::col<&User::profile, &Profile::city>() == "Warsaw");
```

Nullable one-to-one relations declared as `std::optional<RelatedModel>` use the same query paths. Their local
foreign-key columns can be tested with `isNull()` through the related primary-key path:

```cpp
query.where(orm::query::col<&User::profile, &Profile::id>().isNull());
```

By default related models are joined. If `disableJoining()` is used, only related id fields are available in query paths.

Collection fields are not column-path prefixes. Use the collection predicate
helpers instead of `col<&Author::books, &Book::title>()`; such paths are rejected. Collections
also cannot be used as `ORDER BY` or `GROUP BY` expressions, projection fields,
aggregate arguments, or update targets.

Field references retain their root model and scalar type. Query predicates,
ordering, grouping, projection sources, HAVING, and update assignments must
belong to that model. Composing expressions from different root models is a
compile error.

Numeric values allow only conversions that preserve the source type's whole
range and precision, such as `short` to `int` and `float` to `double`. Narrowing,
unsafe signedness changes, floating-point to integer conversion, and
integer-to-floating conversion with insufficient precision are compile errors.
The rule uses the C++ type, even when the current value is small. Boolean
values are separate from numbers; text fields accept string literals,
`std::string`, and `std::string_view`. LIKE is restricted to text.

NULL predicates are available only on optional columns and paths through
optional to-one relations. Filter values cannot be `std::optional<T>`; use a
value or an explicit null predicate. Empty IN/NOT IN lists remain runtime
errors.

## Ordering

Use `asc` and `desc` to add `ORDER BY` clauses:

```cpp
using namespace orm::query;

orm::Query<User> query;
query.orderBy(desc(col<&User::age>()), asc(col<&User::id>()));
```

Ordering can also use related field paths:

```cpp
query.orderBy(asc(col<&User::profile, &Profile::city>()));
```

## Distinct

Use `distinct()` to render `SELECT DISTINCT`:

```cpp
orm::Query<User> query;
query.distinct();
```

## Limit and offset

Use `limit` and `offset` to page through results:

```cpp
orm::Query<User> query;
query.orderBy(orm::query::asc(orm::query::col<&User::id>()))
     .limit(25)
     .offset(50);
```

SQL clauses are rendered in this order:

```sql
WHERE ... GROUP BY ... HAVING ... ORDER BY ... LIMIT ... OFFSET ...
```

## Raw SQL fragments

Raw predicates and raw ordering are available for cases not covered by the ORM DSL.

```cpp
using namespace orm::query;

orm::Query<User> query;
query.where(raw<User>("LOWER(users.name) = :name", param("name", "wojtek")))
     .orderBy(rawOrder<User>("LOWER(users.name) ASC"));
```

Raw SQL text is inserted into the generated query as-is. Values should still be passed as parameters with `param`.

Raw parameter names:

* must not be empty,
* must not include the leading `:`,
* must be unique within the query,
* must not use the reserved `orm_p` prefix.

## Partial-result queries

Use `orm::ProjectionQuery<Source, Result>` to select a subset of fields into a
flat DTO:

```cpp
using namespace orm::query;

struct UserSummary
{
    int id;
    std::string name;
};

orm::ProjectionQuery<User, UserSummary> query;
query.project(as("id", col<&User::id>()),
              as("name", col<&User::name>()))
     .where(col<&User::email>().isNotNull())
     .orderBy(asc(col<&User::id>()));

std::vector<UserSummary> users = database.select(query);
```

Projection aliases must match the DTO field names. See
[Partial-result queries](partial-result-queries.md) for the full contract and
validation rules.

## Full-model grouping and HAVING

`Query<Model>` supports `GROUP BY` and aggregate `HAVING` predicates without
changing its result type:

```cpp
using namespace orm::query;

orm::Query<User> query;
query.where(col<&User::active>() == true)
     .groupBy(col<&User::profile, &Profile::city>())
     .having(countAll<User>() > 2)
     .andHaving(avg(col<&User::age>()) >= 18.0)
     .orderBy(asc(col<&User::profile, &Profile::city>()));

std::vector<User> users = database.select(query);
```

The result remains `std::vector<Model>`. Aggregate expressions are used only
inside `HAVING`; they are not added to the `SELECT` list. Supported helpers are
`count(col(...))`, `countAll<User>()`, `sum(col(...))`, `avg(col(...))`,
`min(col(...))`, and `max(col(...))`.

`having` replaces the aggregate predicate. Use `andHaving` and `orHaving` to
combine predicates, or compose them directly with `&&`, `||`, and `!`.
Comparison values are always sent as bind parameters.

Column mappings, one-level relation paths, and `disableJoining()` follow the
same rules as `WHERE` and `ORDER BY`. Without joins, only related primary-key
paths are available.

SQLite permits a full-model `SELECT` grouped by only some model fields. In that
case each returned model is a representative row for its group, and values of
fields outside `GROUP BY` are not deterministic. Group by every selected field
when deterministic full-model values are required.

PostgreSQL rejects full-model `GROUP BY` and `HAVING` through the backend
capability check before executing SQL. Use an aggregate `ProjectionQuery`
instead. PostgreSQL also requires every non-aggregate projected column and
typed `ORDER BY` column in an aggregate projection to appear in `GROUP BY`.
These rules keep the same typed query API while avoiding backend-dependent
hydration of an incomplete model.

## Aggregate projection queries

Aggregate queries use `ProjectionQuery<Source, Result>` and hydrate flat DTOs
through explicit aliases:

```cpp
using namespace orm::query;

struct CityStats
{
    std::optional<std::string> city;
    long long users;
    std::optional<double> averageAge;
};

orm::ProjectionQuery<User, CityStats> query;
query.project(as("city", col<&User::profile, &Profile::city>()),
              as("users", countAll<User>()),
              as("averageAge", avg(col<&User::age>())))
     .groupBy(col<&User::profile, &Profile::city>())
     .having(countAll<User>() > 1)
     .andHaving(avg(col<&User::age>()) >= 18.0);

std::vector<CityStats> stats = database.select(query);
```

Supported aggregate helpers are `count(col(...))`, `countAll<User>()`,
`sum(col(...))`, `avg(col(...))`, `min(col(...))`, and `max(col(...))`.
`HAVING` predicates compare aggregate expressions to values and can be combined
with `&&`, `||`, `!`, `having`, `andHaving`, and `orHaving`.

`GROUP BY` and aggregate column paths follow the same one-level relation rules
as `WHERE`, `ORDER BY`, and column projections. With `disableJoining()`, related
paths are limited to related primary-key fields.

The aggregate predicate DSL is shared with full-model `Query<Model>`. Use a
projection query only when aggregate values themselves must be returned.

`count` and `countAll` are usually represented as `long long` DTO fields, while
`avg` is usually represented as `double`. Use `std::optional<T>` for aggregate
results that may be SQL `NULL`.

## Write predicates

The same predicate DSL is used by `orm::Update<Model>` and `orm::Database::remove<Model>`:

```cpp
using namespace orm::query;

orm::Update<User> update;
update.set(col<&User::email>(), "new-email@example.com")
      .where(col<&User::id>() == 1);

auto updatedRows = database.update(update);
auto removedRows = database.remove<User>(col<&User::email>().isNull());
```

Write predicates do not generate joins. They support direct model fields and related primary-key paths that can be mapped
to local foreign-key columns:

```cpp
update.where(col<&User::profile, &Profile::id>() == 10);
```

Non-primary-key related paths in write predicates, such as `col<&User::profile, &Profile::city>()`, are compile errors.
Assignments use the same rule: direct fields are supported, and related primary-key assignments update the local
foreign-key column.

## Limitations

The query language currently covers ORM-style `SELECT` returning full model objects plus predicate-based `UPDATE` and
`DELETE` operations.
It supports the dedicated correlated `EXISTS` forms exposed by `any`, `exists`,
and `none`, but not general subqueries or nested collection predicates. It also
does not support nested includes, collection ordering, raw aggregate
expressions, aggregate `ORDER BY`, or `COUNT(DISTINCT ...)`. Fields, relation paths, source ownership, values, and update targets are checked
at compile time. DTO aliases, join settings, builder state, backend capabilities,
and database errors remain runtime checks. Raw SQL uses `raw<Model>` and
`rawOrder<Model>`; its SQL contents cannot be checked by C++ compilation.
The text-field API is removed in 0.2; see [Migration to typed queries](migration-typed-queries.md).
