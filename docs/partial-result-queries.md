# Partial-result queries

Partial-result queries select a subset of model fields into a flat user-defined
DTO. Use them when you do not need to hydrate a full model object.

## Contract

Full-model selects keep the existing API and result type:

```cpp
orm::Query<User> query;
std::vector<User> users = database.select(query);
```

Partial-result selects use a separate query type:

```cpp
template <typename Source, typename Result>
class orm::ProjectionQuery;
```

`Source` is the mapped ORM model. `Result` is a user-defined DTO with the exact
fields returned by the projection. `ProjectionQuery<Source, Result>` is the only
public entry point for partial-result queries; `orm::Query<Model>` remains
reserved for full-model results.

The public projection helper lives in `orm::query`:

```cpp
namespace orm::query
{
struct Projection;

auto as(std::string resultField, Column sourceColumn) -> Projection;
auto as(std::string resultField, AggregateExpression aggregate) -> Projection;

auto count(Column sourceColumn) -> AggregateExpression;
auto countAll() -> AggregateExpression;
auto sum(Column sourceColumn) -> AggregateExpression;
auto avg(Column sourceColumn) -> AggregateExpression;
auto min(Column sourceColumn) -> AggregateExpression;
auto max(Column sourceColumn) -> AggregateExpression;
} // namespace orm::query
```

Projection aliases are explicit. The alias passed to `as` names a field on the
DTO result type, while `col` references a field path on the source model.

```cpp
using namespace orm::query;

struct User
{
    int id;
    std::string displayName;
    std::string email;
};

struct UserSummary
{
    int id;
    std::string name;
};

orm::ProjectionQuery<User, UserSummary> query;
query.project(as("id", col("id")),
              as("name", col("displayName")));

std::vector<UserSummary> rows = database.select(query);
```

## Query behavior

`ProjectionQuery` supports the same filtering, ordering, paging, distinct,
and join controls as `Query`:

```cpp
using namespace orm::query;

orm::ProjectionQuery<User, UserSummary> query;
query.project(as("id", col("id")),
              as("name", col("displayName")))
     .where(col("email").isNotNull())
     .orderBy(asc(col("id")))
     .limit(25)
     .offset(50);
```

The supported builder methods are:

* `where`
* `andWhere`
* `orWhere`
* `orderBy`
* `distinct`
* `limit`
* `offset`
* `disableJoining`
* `groupBy`
* `having`
* `andHaving`
* `orHaving`

Projection field paths use the same one-level relation rules as the existing
query DSL. Relation fields are flattened into DTO fields through aliases:

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

struct UserLocation
{
    int id;
    std::string city;
};

orm::ProjectionQuery<User, UserLocation> query;
query.project(as("id", col("id")),
              as("city", col("profile.city")));
```

Nullable result fields are represented with `std::optional<T>`:

```cpp
struct UserEmail
{
    int id;
    std::optional<std::string> email;
};

orm::ProjectionQuery<User, UserEmail> query;
query.project(as("id", col("id")),
              as("email", col("email")));
```

## Aggregate queries

Aggregate queries also use `ProjectionQuery<Source, Result>`. Aggregated values
are projected into DTO fields through the same explicit `as("dtoField", ...)`
aliases:

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
     .having(countAll() > 1);
```

Supported v1 aggregate helpers are `count(col(...))`, `countAll()`,
`sum(col(...))`, `avg(col(...))`, `min(col(...))`, and `max(col(...))`.
`countAll()` renders `COUNT(*)`; the other helpers validate and render a source
column path.

`HAVING` uses aggregate predicates instead of regular column predicates:

```cpp
query.having(countAll() > 1)
     .andHaving(avg(col("age")) >= 18.0)
     .orHaving(!(max(col("age")) < 65));
```

Comparison values are bound as SQL parameters, the same way `WHERE` predicate
values are bound.

Common DTO field choices are:

* `long long` for `count` and `countAll`,
* `double` for `avg`,
* the database result type for `sum`, `min`, and `max`,
* `std::optional<T>` when the aggregate can return SQL `NULL`, such as `avg`
  or `sum` over an empty result set.

## Result DTO rules

Projection DTOs are flat result objects. Supported DTO field types are the
scalar types already supported by model binding plus `std::optional<T>` for SQL
`NULL` values.

Alias validation is part of the public contract:

* every DTO field must have exactly one projection alias,
* every projection alias must match a DTO field,
* duplicate aliases are invalid,
* invalid source column paths are invalid for the projection,
* relation fields must be projected as flat DTO fields.

Alias validation failures throw `std::invalid_argument` before executing
SQL.

## V1 limitations

Projection DTOs are flat. Relation fields must be flattened through aliases,
for example `as("city", col("profile.city"))`.

Aggregate `ORDER BY`, `COUNT(DISTINCT ...)`, raw aggregate expressions,
`GROUP BY` on full-model `Query<Model>`, subqueries, and `EXISTS` are not part
of this version.
