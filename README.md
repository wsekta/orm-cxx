<div align="center">
  <h1>ORM C++</h1>
  <p>ORM Library for Modern C++ (C++20) using compile-time reflection.</p>

[![C++](https://img.shields.io/badge/C++20-grey.svg?style=flat&logo=c%2B%2B&logoColor=blue)](https://en.cppreference.com/w/cpp/20)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Maintenance](https://img.shields.io/badge/Maintained%3F-yes-green.svg)](https://github.com/wsekta/orm-cxx/graphs/commit-activity)
[![Generic badge](https://img.shields.io/badge/gcc-15+-blue.svg)](https://gcc.gnu.org/)
[![Generic badge](https://img.shields.io/badge/clang-18+-blue.svg)](https://clang.llvm.org/)
[![Generic badge](https://img.shields.io/badge/MSVC-19.50+-blue.svg)](https://en.wikipedia.org/wiki/Microsoft_Visual_Studio)
[![PRs Welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg?style=flat)](https://github.com/wsekta/orm-cxx/issues/new)

[![clang++](https://github.com/wsekta/orm-cxx/actions/workflows/linux-clang-build.yml/badge.svg?branch=main)](https://github.com/wsekta/orm-cxx/actions/workflows/linux-clang-build.yml?select=branch%3Amain)
[![g++](https://github.com/wsekta/orm-cxx/actions/workflows/linux-gxx-build.yml/badge.svg?branch=main)](https://github.com/wsekta/orm-cxx/actions/workflows/linux-gxx-build.yml?select=branch%3Amain)
[![msvc](https://github.com/wsekta/orm-cxx/actions/workflows/windows-msvc-build.yml/badge.svg?branch=main)](https://github.com/wsekta/orm-cxx/actions/workflows/windows-msvc-build.yml?select=branch%3Amain)
[![codecov](https://codecov.io/github/wsekta/orm-cxx/graph/badge.svg?token=MREUNGY5C9)](https://codecov.io/github/wsekta/orm-cxx)
</div>

## 🎯 Goal

The goal of the ORM C++ is to provide a decent Object-Relational Mapping library for C++ community.

🆕 Compile-time model reflection implemented with standard C++20<br>
✅ 100% test coverage with unit tests and integration tests<br>
🗂️ SQLite, PostgreSQL, MySQL and MariaDB support, backed by a shared [portability contract](docs/backend-portability.md)<br>
⚙️ Support for multiple compilers<br>
✅ Native C++20 modules with no project-defined macros<br>
🚀 As low as possible runtime overhead<br>
👶 Easy to use<br>
📉 As few dependencies as possible<br>

## ⚙️ Usage

### First application

Start with the [SQLite quickstart](docs/quickstart.md). It includes a complete
CMake project, Linux and Windows commands, and a checked CRUD example. No
running database server or central package publication is needed.

```cpp
#include <iostream>
#include <string>
#include <vector>

import orm;

struct User { int id; std::string name; };

int main()
{
    orm::Database database{orm::SqliteConfig{.database = orm::SqliteMemory{}}};
auto context = database.orm<orm::Schema<User>>();

    context.createTable<User>();
    context.insert(User{1, "Ada"});

    orm::Query<User> query;
    for (const auto& user : context.select(query))
        std::cout << user.id << ' ' << user.name << '\n';
}
```

With SQLite enabled, this prints `1 Ada`. The model and query API is shared by
all four adapters; see [Backends](docs/backends.md) for build options and typed configurations.
For existing applications, the [package guide](docs/packaging.md) covers vcpkg
and Conan. Central catalog installation requires acceptance of the package
submissions.

## 📖 Documentation

- [Start here and browse the guides](docs/main.md)
- [Collection relations](docs/relations.md)
- [Schema migrations](docs/schema-migrations.md)
- [Doxygen API reference](https://wsekta.github.io/orm-cxx/)
- [Roadmap](ROADMAP.md)

## Backend selection

| Adapter | Build option | Configuration |
| --- | --- | --- |
| SQLite | `ORM_CXX_ENABLE_SQLITE_BACKEND=ON` | `SqliteConfig` |
| PostgreSQL | `ORM_CXX_ENABLE_POSTGRESQL_BACKEND=ON` | `PostgresConfig` |
| MySQL | `ORM_CXX_ENABLE_MYSQL_BACKEND=ON` | `MysqlConfig` |
| MariaDB | `ORM_CXX_ENABLE_MARIADB_BACKEND=ON` | `MariadbConfig` |

All options default to `OFF`. Construct `Database{config}` with owned typed
fields; omitted fields preserve native client defaults. See [Backends](docs/backends.md)
for client dependencies, authentication, TLS and live-test setup.

## Features and integration

- [Models](docs/model.md): aggregate reflection, scalar and nullable fields,
  typed names, closed schemas, composite and generated keys.
- [Database operations](docs/database.md): CRUD, stable errors and transactions.
- [Queries](docs/query.md), [DTO projections](docs/partial-result-queries.md) and
  [immutable plans](docs/static-queries.md): typed predicates, bound values,
  grouping and reusable SQL shapes.
- [Relations](docs/relations.md): to-one mappings, explicit one-to-many and
  many-to-many collections, includes and link mutations.
- [Schema migrations](docs/schema-migrations.md): offline snapshots and reviewed
  diffs, backend SQL generation, immutable versions and explicit recovery.

The public C++20 modules are `orm` and the independent `orm.reflection`.
Applications link `orm-cxx::orm-cxx` or `orm-cxx::reflection`. Source builds and
installed consumers need CMake 3.31+, Ninja 1.11+ and a supported compiler.
Packages include module sources; CMake builds BMIs locally for the consumer.
See [CMake consumers](docs/modules.md#cmake-consumers-and-installation) and
[package installation](docs/packaging.md) for integration details.

## Development

From a checkout with recursive submodules and Docker Compose:

```sh
docker compose run --build --rm dev bash ./scripts/check-fast.sh
```

That runs the Clang 18 core build, tests and quality checks. See
[CONTRIBUTING.md](CONTRIBUTING.md) for native Windows/MSVC setup, all-backend
profiles, documentation checks and the complete CI-to-local command matrix.

## ⚒️ Compiler support

- [MSVC➚](https://en.wikipedia.org/wiki/Microsoft_Visual_Studio) version 19.50 or newer.
- [GCC➚](https://gcc.gnu.org/) version 15 or newer.
- [Clang➚](https://clang.llvm.org/) version 18 or newer.

## 📦 Dependencies

- [GTest](https://github.com/google/googletest) (`ORM_CXX_BUILD_TESTS=OFF` CMake option to disable)
- [SOCI](https://github.com/SOCI/soci)
- SQLite development libraries (when `ORM_CXX_ENABLE_SQLITE_BACKEND=ON`)
- PostgreSQL `libpq` development libraries (when `ORM_CXX_ENABLE_POSTGRESQL_BACKEND=ON`)
- Oracle MySQL C client (when `ORM_CXX_ENABLE_MYSQL_BACKEND=ON` or `ORM_CXX_ENABLE_MARIADB_BACKEND=ON`)

## ✨ Contributing

Feel free to join ORM C++ development! 🚀

Please check [CONTRIBUTING](https://github.com/wsekta/orm-cxx/blob/main/CONTRIBUTING.md) guide.
