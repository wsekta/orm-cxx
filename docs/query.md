# Query

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
14. [Current limitations](#current-limitations)

## Build select

To query objects from a table use `orm::Query<Model>` and pass it to `orm::Database::select`.
The result is always `std::vector<Model>`.

```cpp
#include "orm-cxx/orm.hpp"

struct User
{
    int id;
    std::string name;
    std::string email;
    int age;
};

orm::Database database;
database.connect("sqlite3://test.db");

orm::Query<User> query;
auto users = database.select(query);
```

`Query` is a builder, so methods can be chained:

```cpp
using namespace orm::query;

orm::Query<User> query;
query.where(col("age") >= 18)
     .orderBy(desc(col("id")))
     .limit(10)
     .offset(20);
```

## Loading collections

`OneToMany` and `ManyToMany` fields are unloaded by default. Request a
collection explicitly on a full-model query:

```cpp
orm::Query<Author> query;
query.include("books")
     .orderBy(orm::query::asc(orm::query::col("id")))
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
query.where((col("age") >= 18 && col("name").like("Ann%")) || col("email").isNull());
```

Supported operators:

| C++ API | SQL |
| --- | --- |
| `col("age") == 18` | `=` |
| `col("age") != 18` | `!=` |
| `col("age") > 18` | `>` |
| `col("age") >= 18` | `>=` |
| `col("age") < 18` | `<` |
| `col("age") <= 18` | `<=` |
| `col("name").like("Ann%")` | `LIKE` |
| `col("name").notLike("Ann%")` | `NOT LIKE` |
| `col("email").isNull()` or `col("email") == nullptr` | `IS NULL` |
| `col("email").isNotNull()` or `col("email") != nullptr` | `IS NOT NULL` |
| `col("age").in({18, 19, 20})` | `IN` |
| `col("age").notIn({18, 19, 20})` | `NOT IN` |
| `col("age").between(18, 30)` | `BETWEEN` |
| `col("age").notBetween(18, 30)` | `NOT BETWEEN` |

Logical operators are available with `&&`, `||` and `!`.

```cpp
query.where(!(col("name").like("test%") || col("email").isNull()));
```

Calling `where` replaces the current predicate. Use `andWhere` or `orWhere` to append to the current predicate:

```cpp
query.where(col("age") >= 18)
     .andWhere(col("email").isNotNull())
     .orWhere(col("name") == "admin");
```

All comparison values are sent to the database as SOCI bind parameters. They are not interpolated into SQL strings.

## Collection predicates

Use `any`, `exists`, and `none` to filter a model by a mapped collection:

```cpp
using namespace orm::query;

orm::Query<Author> query;
query.where(any("books", col("title").like("C++%")) &&
            none("books", col("title").like("Draft%")));

orm::Query<Author> nonEmpty;
nonEmpty.where(exists("books"));
```

`any("books", predicate)` renders a correlated `EXISTS` subquery.
`none("books", predicate)` renders `NOT EXISTS`, and `exists("books")` checks
only whether at least one related row exists. They work for both `OneToMany`
and `ManyToMany`, including inverse many-to-many mappings.

The element predicate is resolved relative to the target model. It may contain
target scalar fields and that model's existing one-level to-one paths. Values
are bound normally; they are never interpolated into SQL. Nested collection
predicates are rejected.

A collection predicate filters only. It does not mark the wrapper loaded or
fetch its elements; combine it with `include("books")` when both behaviors are
needed.

## Column names and relations

`col("field")` uses the C++ model field name. If the model defines `columns_names`, the query renderer maps the field name to the configured database column name.

```cpp
struct User
{
    int id;
    std::string displayName;

    inline static const std::map<std::string, std::string> columns_names = {
        {"displayName", "display_name"},
    };
};

orm::Query<User> query;
query.where(orm::query::col("displayName") == "Wojtek");
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
    Profile profile;
};

orm::Query<User> query;
query.where(orm::query::col("profile.city") == "Warsaw");
```

Nullable one-to-one relations declared as `std::optional<RelatedModel>` use the same query paths. Their local
foreign-key columns can be tested with `isNull()` through the related primary-key path:

```cpp
query.where(orm::query::col("profile.id").isNull());
```

By default related models are joined. If `disableJoining()` is used, only related id fields are available in query paths.

Collection fields are not column-path prefixes. Use the collection predicate
helpers instead of `col("books.title")`; such paths are rejected. Collections
also cannot be used as `ORDER BY` or `GROUP BY` expressions, projection fields,
aggregate arguments, or update targets.

The typed helper is also available when you want to document the expected field type at the call site:

```cpp
query.where(orm::query::field<User, int>("age") >= 18);
```

The typed helper still takes the field name as a string. It does not infer field names from member pointers.

## Ordering

Use `asc` and `desc` to add `ORDER BY` clauses:

```cpp
using namespace orm::query;

orm::Query<User> query;
query.orderBy(desc(col("age")), asc(col("id")));
```

Ordering can also use related field paths:

```cpp
query.orderBy(asc(col("profile.city")));
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
query.orderBy(orm::query::asc(orm::query::col("id")))
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
query.where(raw("LOWER(users.name) = :name", param("name", "wojtek")))
     .orderBy(rawOrder("LOWER(users.name) ASC"));
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
query.project(as("id", col("id")),
              as("name", col("displayName")))
     .where(col("email").isNotNull())
     .orderBy(asc(col("id")));

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
query.where(col("active") == true)
     .groupBy(col("profile.city"))
     .having(countAll() > 2)
     .andHaving(avg(col("age")) >= 18.0)
     .orderBy(asc(col("profile.city")));

std::vector<User> users = database.select(query);
```

The result remains `std::vector<Model>`. Aggregate expressions are used only
inside `HAVING`; they are not added to the `SELECT` list. Supported helpers are
`count(col(...))`, `countAll()`, `sum(col(...))`, `avg(col(...))`,
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

## Aggregate projection queries

Aggregate queries use `ProjectionQuery<Source, Result>` and hydrate flat DTOs
through explicit aliases:

```cpp
using namespace orm::query;

struct CityStats
{
    std::string city;
    long long users;
    double averageAge;
};

orm::ProjectionQuery<User, CityStats> query;
query.project(as("city", col("profile.city")),
              as("users", countAll()),
              as("averageAge", avg(col("age"))))
     .groupBy(col("profile.city"))
     .having(countAll() > 1)
     .andHaving(avg(col("age")) >= 18.0);

std::vector<CityStats> stats = database.select(query);
```

Supported aggregate helpers are `count(col(...))`, `countAll()`,
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
update.set(col("email"), "new-email@example.com")
      .where(col("id") == 1);

auto updatedRows = database.update(update);
auto removedRows = database.remove<User>(col("email").isNull());
```

Write predicates do not generate joins. They support direct model fields and related primary-key paths that can be mapped
to local foreign-key columns:

```cpp
update.where(col("profile.id") == 10);
```

Non-primary-key related paths in write predicates, such as `col("profile.city")`, throw `std::invalid_argument`.
Assignments use the same rule: direct fields are supported, and related primary-key assignments update the local
foreign-key column.

## Current limitations

The query language currently covers ORM-style `SELECT` returning full model objects plus predicate-based `UPDATE` and
`DELETE` operations.
It supports the dedicated correlated `EXISTS` forms exposed by `any`, `exists`,
and `none`, but not general subqueries or nested collection predicates. It also
does not support nested includes, collection ordering, raw aggregate
expressions, aggregate `ORDER BY`, or `COUNT(DISTINCT ...)`.
