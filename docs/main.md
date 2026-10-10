# ORM C++

ORM C++ is a C++20 ORM that maps plain aggregate structs to database tables
using compile-time reflection. It ships with SQLite and PostgreSQL backends
built on SOCI.

The public API consists of two named modules:

```cpp
#include <string>
import orm;
```

`import orm.reflection;` provides the independent reflection layer. Link
`orm-cxx::orm-cxx` or `orm-cxx::reflection` respectively. Both are compiled CMake
targets. Standard-library headers remain ordinary includes.

**Build requirements:** CMake 3.31+, Ninja 1.11+,
GCC 15+, Clang 18+, or MSVC 19.50+. Installed packages include module interface
sources and native CMake metadata; CMake builds BMIs locally.

1. [Model](model.md)
2. [Collection relations](relations.md)
3. [Query](query.md)
4. [Static query plans](static-queries.md)
5. [Database](database.md)
6. [Backends](backends.md)
7. [Backend portability](backend-portability.md)
8. [Backend extension contract](backend-extension.md)
9. [Package managers and releases](packaging.md)
10. [C++20 modules and CMake consumers](modules.md)
11. [Schema migrations](schema-migrations.md)

## Build configuration and macro policy

Backend options are selected when building the library or package. The `orm`
module exports `orm::config::sqliteBackendEnabled` and
`orm::config::postgresqlBackendEnabled` as C++ constants. Use `if constexpr`
for backend-dependent behavior; expressions whose validity depends on a backend
must remain dependent inside a template.

First-party code and exported package metadata define no preprocessor macros.
`scripts/check-macros.py` checks module sources, C++ sources, templates, CMake,
and Conan recipes. Backend selection uses generated C++ constants, source
selection, and dependency linkage. CMake cache `-D` options are permitted.
Compiler conditionals are confined to reflection adapters and platform tests;
external-library macros such as GoogleTest assertions are outside this policy.

The API may change freely while the project has no established external users.
Documentation describes the current API; version-to-version migration guides
and compatibility fallbacks are not maintained during this stage.
