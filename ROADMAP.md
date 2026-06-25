# ORM C++ Roadmap

This roadmap is focused on making `orm-cxx` easier to adopt as an open source
C++ ORM: complete the core SQLite workflow first, stabilize the public API, then
expand database support.

## Current State

`orm-cxx` already provides a usable SQLite-first foundation:

- C++20 model metadata based on reflected struct fields.
- Table creation and drop commands.
- Full row-level CRUD operations through `orm::Database`, including insert,
  select, predicate-based update, and predicate-based remove operations.
- A query builder for full-model `SELECT` queries with predicates, ordering,
  `DISTINCT`, `LIMIT`, `OFFSET`, raw predicates, raw ordering, and bind
  parameters.
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

The next priority is to tighten the SQLite model behavior and documentation now
that basic CRUD is available.

- Add auto-increment primary key support, starting with SQLite semantics.
- Align the model documentation with the currently supported scalar types,
  optional fields, primary-key behavior, and one-to-one relations.
- Remove duplicated or stale backlog items as work moves into documented
  milestones.

## Mid Term

With basic CRUD complete, improve query expressiveness and contributor
experience.

- Extend the query DSL with projections, aggregate functions, `GROUP BY`, and
  `HAVING`.
- Decide the public shape for partial-result queries before implementing
  projections, so result types are predictable and documented.
- Plan relation support beyond one-to-one, including `OneToMany` and
  `ManyToMany` mappings.
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
