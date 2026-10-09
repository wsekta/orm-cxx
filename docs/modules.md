# C++20 modules

The public API has two named modules. `orm` provides models, query expressions,
static query plans, database operations, and the configured backends. It also
re-exports the reflection API. `orm.reflection` provides compile-time reflection
independently of SOCI and database backends.

```cpp
#include <string>

import orm;

struct Person
{
    int id;
    std::string name;
};
```

Applications import these primary modules. The partitions below organize the
implementation; they are not separate consumer entry points. Standard-library
facilities used by application code still require their ordinary headers.

## Partition dependencies

Each row lists the partitions that its module unit directly imports. Names
beginning with `:` belong to `orm`, except `:generated` in `orm.reflection`.

| Module unit | Direct imports |
| --- | --- |
| `orm.reflection:generated` | None |
| `orm.reflection` | `:generated` |
| `orm:foundation` | `orm.reflection` |
| `orm:model` | `orm.reflection`, `:foundation` |
| `orm:expressions` | `orm.reflection`, `:foundation`, `:model` |
| `orm:dynamic_query` | `orm.reflection`, `:foundation`, `:model`, `:expressions` |
| `orm:static_plan` | `orm.reflection`, `:foundation`, `:model`, `:expressions`, `:dynamic_query` |
| `orm:sql` | `orm.reflection`, `:foundation`, `:model`, `:expressions`, `:static_plan` |
| `orm:config` | None |
| `orm:backend_relations` | `:foundation`, `:model`, `:expressions`, `:sql` |
| `orm:database` | `orm.reflection`, `:foundation`, `:model`, `:expressions`, `:dynamic_query`, `:static_plan`, `:sql`, `:config`, `:backend_relations` |
| `orm:internal` | `:foundation`, `:model`, `:expressions`, `:dynamic_query`, `:static_plan`, `:sql`, `:database` |
| `orm` | `orm.reflection` and all interface partitions of `orm` |

This graph is acyclic. CMake generates `:config` and `:backend_relations` for
the selected backend configuration. `:internal` is an implementation partition
used by the library's implementation units. `orm.reflection:generated` contains
the generated aggregate binding machinery.

## CMake consumers and installation

Use CMake 3.31+, Ninja 1.11+, and GCC 15+, Clang 18+, or
MSVC 19.50+. Use separate build directories for Debug and Release.
Link the target matching the imported module:

```cmake
cmake_minimum_required(VERSION 3.31)
project(application LANGUAGES CXX)

find_package(orm-cxx CONFIG REQUIRED)
add_executable(application main.cpp)
target_link_libraries(application PRIVATE orm-cxx::orm-cxx)
```

For an application that imports only `orm.reflection`, use
`find_package(orm-cxx-reflection CONFIG REQUIRED)` and
`target_link_libraries(application PRIVATE orm-cxx::reflection)`.
Both targets are compiled libraries and propagate the C++20 requirement.
For GCC they also propagate `-fno-module-lazy` to producers, importers, and
installed interfaces rebuilt by CMake. GCC 14 exceeds its imported source
location limit; GCC 15 and 16 need this option to avoid the lazy reader
reporting `Bad file data`.

Installed packages provide the libraries, the `.cppm` source interfaces and
their private source includes under `share/orm-cxx/modules`, and native CMake
exports with `CXX_MODULES` file sets and import metadata. CMake scans imports
and compiles the required binary module interfaces (BMIs) in the consumer's
build directory. The configured backend interface sources remain part of that
installation.

BMIs depend on the compiler, compiler version, standard library, and build
settings. They are not installed as portable package artifacts. Use compatible
compiler and ABI settings for the installed libraries and the consuming
application. The ORM's SOCI dependencies provide the headers and libraries
needed to compile and link its module interfaces; the independent reflection
target does not require SOCI.

On MSVC, native metadata applies each library's CRT setting to its generated
BMIs. Select the matching runtime for application targets with
`MSVC_RUNTIME_LIBRARY`; a package's metadata does not change that application
setting or the global `CMAKE_MSVC_RUNTIME_LIBRARY` value.

See [package managers and releases](packaging.md) for Conan and vcpkg usage.
