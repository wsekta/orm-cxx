# Quickstart: SQLite CRUD

Build a small application, insert a user, read it, change its name and delete
it. SQLite runs in process; this example needs no database server or credentials.
It uses an in-memory database, so every run starts empty and changes no files.

## Requirements

- Git, CMake 3.31+, Ninja 1.11+ and a C++20 compiler: GCC 15+, Clang 18+
  or MSVC 19.50+.
- On Linux, SQLite development headers and libraries. On Ubuntu 24.04, install
  `clang-18 clang-tools-18 libsqlite3-dev`; install CMake and Ninja separately
  if the distribution versions are too old.
- On Windows, Visual Studio 2026 with **Desktop development with C++**.
  Run commands in its Developer PowerShell. The repository's vcpkg manifest
  provisions SQLite in the commands below.

The public API uses `import orm;`. Ordinary standard-library headers still need
`#include`. All database adapters default to disabled; this application enables
only SQLite. See [backends](backends.md) when choosing another adapter.

## Get the sources

```sh
git clone --recurse-submodules https://github.com/wsekta/orm-cxx.git
cd orm-cxx
```

This source-based route works without publication in a central package catalog.
For existing applications using vcpkg or Conan, see [package installation](packaging.md).

## Build and run

On Linux with Clang 18:

```sh
cmake -S examples/quickstart -B build/quickstart -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++-18
cmake --build build/quickstart
./build/quickstart/bin/quickstart
ctest --test-dir build/quickstart --output-on-failure
```

For GCC, replace `clang++-18` with `g++-15` and use a separate build directory.

On Windows, from Developer PowerShell:

```powershell
./externals/vcpkg/bootstrap-vcpkg.bat
./externals/vcpkg/vcpkg.exe install --x-feature=sqlite
cmake -S examples/quickstart -B build/quickstart -G Ninja -DCMAKE_BUILD_TYPE=Debug "-DCMAKE_TOOLCHAIN_FILE=$PWD/externals/vcpkg/scripts/buildsystems/vcpkg.cmake" "-DVCPKG_MANIFEST_DIR=$PWD" "-DVCPKG_INSTALLED_DIR=$PWD/vcpkg_installed" -DVCPKG_MANIFEST_FEATURES=sqlite
cmake --build build/quickstart
./build/quickstart/bin/quickstart.exe
ctest --test-dir build/quickstart --output-on-failure
```

If you prefer the project's Linux development container, run the same example
from the checkout on a machine with Docker Compose:

```sh
docker compose run --build --rm dev cmake -S examples/quickstart -B build/quickstart-container -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++-18
docker compose run --rm dev cmake --build build/quickstart-container
docker compose run --rm dev ./build/quickstart-container/bin/quickstart
```

Expected output on every run:

<!-- include: examples/quickstart/expected.txt -->
```text
Selected: 1 Ada
Updated: 1 Grace
Deleted: 1
Remaining: 0
```

The executable returns a nonzero exit code if the selected data or affected-row
counts differ. CTest runs those checks too.

## The application

The complete [main.cpp](../examples/quickstart/main.cpp) is:

<!-- include: examples/quickstart/main.cpp -->
```cpp
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <variant>
#include <vector>

import orm;

struct User
{
    int id;
    std::string name;
};

int main()
{
    using namespace orm::query;
    using AppSchema = orm::Schema<User>;

    orm::Database database{orm::SqliteConfig{.database = orm::SqliteMemory{}}};
    auto context = database.orm<AppSchema>();
    context.createTable<User>();

    context.insert(User{1, "Ada"});
    orm::Query<User> query;
    query.where(col<&User::id>() == 1);
    auto users = context.select(query);
    if (users.size() != 1 || users.front().name != "Ada")
        return 1;
    std::cout << "Selected: " << users.front().id << ' ' << users.front().name << '\n';

    orm::Update<User> update;
    update.set(col<&User::name>(), "Grace").where(col<&User::id>() == 1);
    const auto updated = context.update(update);
    users = context.select(query);
    if (updated != 1 || users.size() != 1 || users.front().name != "Grace")
        return 1;
    std::cout << "Updated: " << updated << " Grace\n";

    const auto removed = context.remove<User>(col<&User::id>() == 1);
    users = context.select(query);
    if (removed != 1 || !users.empty())
        return 1;
    std::cout << "Deleted: " << removed << "\nRemaining: " << users.size() << '\n';
}
```

`User` is an aggregate model. Its `id` field is the default primary key.
`Schema<User>` declares the application's model set. `Database` owns the
connection, while `context` borrows it and performs schema-bound operations.
Values in the typed predicates and update are bound parameters.

The complete [CMakeLists.txt](../examples/quickstart/CMakeLists.txt) is:

<!-- include: examples/quickstart/CMakeLists.txt -->
```cmake
cmake_minimum_required(VERSION 3.31)
project(orm-cxx-quickstart LANGUAGES CXX)

set(ORM_CXX_BUILD_TESTS
    OFF
    CACHE BOOL "Build orm-cxx tests"
)
set(ORM_CXX_BUILD_EXAMPLES
    OFF
    CACHE BOOL "Build orm-cxx examples"
)
set(ORM_CXX_ENABLE_SQLITE_BACKEND
    ON
    CACHE BOOL "Build SQLite backend"
)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_subdirectory(../.. orm-cxx)

add_executable(quickstart main.cpp)
set_target_properties(quickstart PROPERTIES CXX_SCAN_FOR_MODULES ON)
target_link_libraries(quickstart PRIVATE orm-cxx::orm-cxx)

enable_testing()
add_test(NAME quickstart COMMAND quickstart)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/expected.txt" quickstart_expected_output)
string(REPLACE "\r" "" quickstart_expected_output "${quickstart_expected_output}")
set_tests_properties(quickstart PROPERTIES PASS_REGULAR_EXPRESSION "${quickstart_expected_output}")
```

This is a standalone consumer project. The runtime output directory keeps the
executable and the bundled SOCI DLLs together on Windows. It disables the
library's development targets and links `orm-cxx::orm-cxx`, which provides the C++20 module requirement.
When adapting it for your own application, replace `../..` in `add_subdirectory`
with the path to the orm-cxx checkout. Installed consumers use
`find_package(orm-cxx CONFIG REQUIRED)` instead; see [CMake consumers](modules.md#cmake-consumers-and-installation).

## If the build fails

- **Unsupported compiler or generator:** use the versions above and `-G Ninja`.
  CMake's default generator may not support this project's modules.
- **Missing `clang-scan-deps`:** install `clang-tools-18` alongside Clang 18.
- **Missing SQLite:** install `libsqlite3-dev` on Linux, or run the vcpkg feature
  installation and pass its toolchain on Windows.
- **Changed compiler, runtime or build type:** configure a fresh build directory;
  compiled module interfaces cannot be reused across incompatible toolchains.

## Next steps

- [Models and closed schemas](model.md): fields, names, keys and validation.
- [Database and CRUD](database.md): connection lifecycle, errors and transactions.
- [Queries](query.md) and [static query plans](static-queries.md).
- [Collection relations](relations.md) and [schema migrations](schema-migrations.md).
- [Contributing](../CONTRIBUTING.md): development containers and the full test matrix.
