# ORM C++ Roadmap

This roadmap is focused on making `orm-cxx` easier to adopt as an open source
C++ ORM: keep the SQLite workflow stable, document the public model and query
contracts, then expand expressiveness and database support deliberately.

## Current State

`orm-cxx` provides a usable SQLite-first foundation:

- C++20 model metadata based on reflected struct fields.
- A documented SQLite model contract for supported scalar fields, nullable
  fields with `std::optional<T>`, table and column name mapping, default and
  explicit primary keys, SQLite auto-increment primary keys, and one-level
  one-to-one relations.
- Nullable one-to-one relations through `std::optional<RelatedModel>`, including
  nullable local foreign-key columns, SQL `NULL` inserts, joined selects, and
  non-joined selects that hydrate related primary-key values.
- Table creation and drop commands.
- Full row-level CRUD operations through `orm::Database`, including insert,
  select, predicate-based update, and predicate-based remove operations.
- A query builder for full-model `SELECT` queries with predicates, ordering,
  `DISTINCT`, `LIMIT`, `OFFSET`, raw predicates, raw ordering, and bind
  parameters.
- A projection query builder for partial-result `SELECT` queries that hydrate
  flat DTOs through explicit field aliases.
- A patch-style `orm::Update<T>` builder for safe, filtered `UPDATE`
  statements, nullable-field updates through `std::nullopt`, and affected-row
  counts for `UPDATE` and `DELETE`.
- One-level one-to-one relation support in model metadata, inserts, select
  joins, related-field query paths, and write operations targeting local
  foreign-key columns through related primary-key paths.
- Unit and integration tests across model metadata, command rendering, query
  behavior, write operations, transactions, and SQLite execution.
- CI coverage for GCC, Clang, MSVC, and Codecov.

## Near Term

The next priority is to improve query expressiveness without making result
types unpredictable.

- Add aggregate functions, `GROUP BY`, and `HAVING` on top of the projection
  design.
- Add examples that show full-model selects, projected selects, and aggregate
  queries side by side.

## Mid Term

After projections are stable, improve contributor experience and relation
coverage.

- Plan relation support beyond one-to-one, including `OneToMany` and
  `ManyToMany` mappings.
- Decide how custom field converters should work before adding date/time, UUID,
  or third-party optional types such as `std::tm`, `boost::uuids::uuid`, and
  `boost::optional`.
- Refactor CMake configuration into clearer library, example, test, and
  dependency boundaries.
- Add `.clang-tidy`, `.cmake-format`, and static-analysis checks in GitHub
  Actions.
- Add a Docker or devcontainer-based development environment for predictable
  local setup.

## Long Term

Once the SQLite API is stable, make database portability a real feature rather
than just an enum-level intention.

- Harden the command-generation contracts needed by non-SQLite backends.
- Add PostgreSQL as the first backend after SQLite.
- Add MySQL, ODBC, Oracle, Firebird, and DB2 according to user demand and
  maintainer capacity.
- Introduce backend-specific integration tests so supported databases are
  verified by behavior, not only by SQL string generation.
- Package the library for vcpkg and Conan once the public API has settled.

## Far Future

These items are valuable, but they should not block the core ORM experience.

- Reduce dependency risk around `reflect-cpp`, or replace it with a dedicated
  reflection layer if that becomes practical.
- Move more metadata and query validation work to compile time.
- Define and document thread-safety guarantees.
- Add structured logging hooks.
- Explore coroutine-based APIs for asynchronous or pipelined database work.

## Roadmap Principles

- Prefer stable, documented behavior over broad but shallow feature coverage.
- Keep SQLite excellent before adding more backends.
- Add public APIs only when their behavior, error cases, and tests are clear.
- Treat documentation and examples as part of the feature, not follow-up polish.
