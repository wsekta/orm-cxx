# Query

1. [Build select](#build-select)
2. [Where predicates](#where-predicates)
3. [Column names and relations](#column-names-and-relations)
4. [Ordering](#ordering)
5. [Distinct](#distinct)
6. [Limit and offset](#limit-and-offset)
7. [Raw SQL fragments](#raw-sql-fragments)
8. [Partial-result queries](#partial-result-queries)
9. [Write predicates](#write-predicates)
10. [Current limitations](#current-limitations)

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
WHERE ... ORDER BY ... LIMIT ... OFFSET ...
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
It does not yet support aggregate functions, `GROUP BY`, `HAVING`, subqueries or `EXISTS`.
