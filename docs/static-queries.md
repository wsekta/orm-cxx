# Static query plans

Version `0.3.0` adds immutable plans alongside the mutable `orm::Query<T>`,
`orm::ProjectionQuery<Source, Result>`, and `orm::Update<T>` builders. A plan
preserves the expression structure in its C++ type. SQLite and PostgreSQL use
that structure to prepare SQL once for the schema, plan type, and SQL dialect.
Execution binds the current values and hydrates the usual model or DTO result.

```cpp
using namespace orm::query;

struct User
{
    int id;
    int age;
    std::string name;
    std::optional<std::string> email;
};
using AppSchema = orm::Schema<User>;

inline constexpr auto adults =
    select<User>().where(col<&User::age>() >= param<int, 0>())
                  .orderBy(asc(col<&User::id>()))
                  .limit(10);

orm::Database<AppSchema> database;
// Connect and create/populate the table before executing the plan.
auto first = database.select(adults, 18);
auto second = database.select(adults, 21);
```

`param<T, I>()` declares a typed argument slot. Indices start at zero and must
be contiguous. Reusing an index reuses its execution argument; every occurrence
must declare the same type. Calls must supply exactly one compatible argument
per slot in index order. The existing rules for safe numeric widening, scalar
filter values, nullable fields, model ownership, and writable paths also apply
to plans. A filter on an optional field takes its scalar type; `std::optional`
arguments are available for nullable `SET` values.

Clause methods return a new plan. Keep the returned value:

```cpp
constexpr auto base = select<User>();
constexpr auto filtered = base.where(col<&User::age>() >= param<int, 0>());
constexpr auto ordered = filtered.orderBy(asc(col<&User::name>()));
```

The objects may also carry ordinary runtime values. `constexpr` is useful for
reusable templates with argument slots, but is not required for using a plan.

## Projections and writes

The literal alias form `as<"alias">(expression)` retains the alias in the plan
type. The existing `as("alias", expression)` form supplies a runtime alias.

```cpp
struct UserName { std::string name; };

constexpr auto names =
    selectAs<User, UserName>(as<"name">(col<&User::name>()))
        .where(col<&User::age>() >= param<int, 0>());
auto summaries = database.select(names, 18);

constexpr auto rename =
    update<User>().set(col<&User::name>(), param<std::string, 0>())
                  .where(col<&User::id>() == param<int, 1>());
auto changed = database.update(rename, std::string{"Ada"}, 1);

constexpr auto erase = remove<User>().where(col<&User::id>() == param<int, 0>());
auto removed = database.remove(erase, 1);
```

An `UPDATE` plan requires at least one `SET` assignment and a `WHERE` predicate.
A `DELETE` plan requires a `WHERE` predicate. These checks happen when the plan
is converted or executed. The full-model select supports collection `include`
and the existing relation predicates; projection plans return flat DTOs.

## Runtime shapes and explicit conversion

Fixed-size `std::array`, C arrays, and variadic `IN` arguments preserve their
arity. Empty fixed-size `IN` expressions are rejected at compile time. Vectors
and initializer lists have runtime cardinality and use the runtime SQL path.
The same applies to raw predicates, raw ordering, and runtime aliases. Those
forms retain the typed field and model checks.

```cpp
auto fixed = select<User>().where(col<&User::id>().in(std::array{1, 2, 3}));
auto variable = select<User>().where(col<&User::id>().in(std::vector{1, 2, 3}));
auto rawPlan = select<User>().where(raw<User>("age > :age", param("age", 18)));
```

`plan.toDynamic(args...)` binds all slots and creates the corresponding mutable
select or update builder. A delete plan returns its bound typed predicate.
Use conversion when later clauses depend on a runtime branch:

```cpp
auto query = adults.toDynamic(18);
if (searchByName)
    query.andWhere(col<&User::name>().like("A%"));
auto rows = database.select(query);
```

Expression `.dynamic()` creates the existing named typed facade and is
available only after all expression values are concrete. Slot-bearing
expressions must first be bound through their plan. See the
[0.3 migration guide](migration-static-queries.md) for expression return types
and mutable-expression variables, and
[the runnable example](../examples/static_queries.cpp) for a complete program.
