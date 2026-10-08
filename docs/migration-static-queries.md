# Migrating queries to 0.3

Version `0.3.0` adds static plans and changes typed expression factories to
return expression-template types. Member-pointer fields and the 0.2 model,
operator, nullability, safe-widening, and write-safety contracts remain in force.

Existing mutable query builders still accept expressions directly:

```cpp
using namespace orm::query;
orm::Query<User> query;
query.where(col<&User::age>() >= 18)
     .orderBy(asc(col<&User::name>()));
```

Use `auto` for expressions whose structure stays fixed:

```cpp
auto predicate = col<&User::age>() >= 18;
query.where(predicate);
```

Different operators or compositions can now have different C++ types. Code
that replaces an inferred predicate with a different expression should retain
the named facade through `.dynamic()`:

```cpp
auto predicate = (col<&User::age>() >= 18).dynamic();
if (searchByName)
    predicate = col<&User::name>().like("A%").dynamic();
query.where(predicate);
```

The named facades remain `TypedPredicate<Model, WriteSafe,
ContainsCollection>`, `TypedOrderBy<Model>`, `TypedProjection<Model>`,
`TypedAggregate<Model, Value, Nullable>`, and `TypedAggregatePredicate<Model>`.
`.dynamic()` preserves all those parameters. A predicate using a related
non-key field cannot be assigned to a facade for a write-safe predicate;
collection presence and aggregate nullability also remain part of the type.
Update explicit function return types or containers of expressions to use the
appropriate named facade and return `.dynamic()` from the expression.

For reusable queries, introduce slots and keep each immutable clause result:

```cpp
constexpr auto adults =
    select<User>().where(col<&User::age>() >= param<int, 0>());
auto rows = database.select(adults, 18);

// Bind first when a mutable builder is needed.
auto mutableQuery = adults.toDynamic(18);
mutableQuery.andWhere(col<&User::name>().like("A%"));
```

An unbound slot cannot enter a mutable builder or `.dynamic()` expression.
Pass exactly one argument per contiguous slot to a plan. Repeated indices must
use the same declared type. The argument conversion uses the existing safe
widening rules; it does not narrow a value merely because that particular
value would fit.

Use `as<"name">(col<&User::name>())` for an alias retained in a static plan.
The existing `as("name", col<&User::name>())` form remains available for runtime
aliases. Fixed-size and variadic `IN` forms retain cardinality, while vectors,
initializer lists, raw predicates, and raw ordering use the runtime SQL path.

See [static query plans](static-queries.md) for selects, projections, updates,
deletes, relation predicates, and runtime fallback.
