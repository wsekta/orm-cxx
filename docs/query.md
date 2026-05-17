# Query

1. [Build select](#build-select)
2. [Where predicates](#where-predicates)
3. [Column names and relations](#column-names-and-relations)
4. [Ordering](#ordering)
5. [Distinct](#distinct)
6. [Limit and offset](#limit-and-offset)
7. [Raw SQL fragments](#raw-sql-fragments)
8. [Current limitations](#current-limitations)

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

## Current limitations

The query language currently covers ORM-style `SELECT` returning full model objects.
It does not yet support projections, aggregate functions, `GROUP BY`, `HAVING`, subqueries, `EXISTS`, `UPDATE` or `DELETE`.
