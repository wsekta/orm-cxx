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

## Future aggregate queries

Aggregate functions, `GROUP BY`, and `HAVING` are intentionally outside the
first projection step. They should reuse the same explicit alias contract so
aggregate values can hydrate DTO fields without introducing tuple or dynamic-row
result types.
