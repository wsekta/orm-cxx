# ORM C++

ORM C++ is a C++20 ORM that maps plain aggregate structs to database tables
using compile-time reflection. It ships with SQLite, PostgreSQL, MySQL and MariaDB backends
built on SOCI.

Import the ORM with its named module:

```cpp
import orm;
```

Link the CMake target `orm-cxx::orm-cxx`.
For standalone compile-time reflection, use the separate module instead:

```cpp
import orm.reflection;
```

Link the CMake target `orm-cxx::reflection`. Both targets are compiled libraries.
Include any standard-library headers used by your application in the usual way.

**Build requirements:** CMake 3.31+, Ninja 1.11+,
GCC 15+, Clang 18+, or MSVC 19.50+. Installed packages include module interface
sources and native CMake metadata; CMake builds BMIs locally.

## Start here

1. [Quickstart: SQLite CRUD](quickstart.md) — a complete application, build commands and expected output.
2. [Model](model.md) — aggregate fields, closed schemas, names and keys.
3. [Database](database.md) — connections, CRUD, errors and transactions.
4. [Query](query.md) — mutable queries and typed predicates.
5. [Partial-result queries](partial-result-queries.md) — flat DTOs and aggregates.
6. [Static query plans](static-queries.md) — reusable immutable query shapes.
7. [Collection relations](relations.md) — mapping, loading and changing links.
8. [Schema migrations](schema-migrations.md) — reviewed model changes and SQL.

## Integration reference

- [Date, time and UUID values](value-types.md): standalone value types and current integration limits.
- [Backends](backends.md): typed configurations, adapter selection and differences.
- [Package installation](packaging.md): vcpkg, Conan and native installation.
- [C++20 modules and CMake consumers](modules.md): toolchain and ABI requirements.

## For contributors and maintainers

- [Contributing](../CONTRIBUTING.md): setup and local verification.
- [Backend portability](backend-portability.md) and [extension contract](backend-extension.md).
- [CI compiler matrix](ci-compiler-matrix.md).
- [Release maintenance](packaging.md#maintainer-setup-and-publishing).

## API reference

The generated reference covers all four optional adapters; an installed library
exports only the adapters selected at build time. Its documentation profile
enables every adapter, so values shown for `orm::config` describe that profile,
not the default core-only build.
