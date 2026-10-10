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

## Source organization

The two primary interfaces are short entry points. Their partitions live in
subdirectories and remain attached to the same named module:

```text
modules/
  orm.cppm
  orm.reflection.cppm
  orm/
    foundation.cppm
    foundation/
    model.cppm
    model/
    expressions.cppm
    query/
    dynamic_query.cppm
    dynamic_query/
    static_plan.cppm
    sql.cppm
    sql/
    database.cppm
    database/
    internal.cppm
  reflection/
    generated.cppm
    generated/
    ...
```

| Directory | Responsibilities |
| --- | --- |
| `orm/foundation` | Utilities, backend and column types, relation collections and descriptors |
| `orm/model` | Mapping, metadata views, column and relation descriptors, static models and schemas |
| `orm/query` | Values, runtime predicates, parameters, typed expressions, columns, aggregates and projections |
| `orm/dynamic_query` | Select, projection and update builders |
| `orm/sql` | Backend contracts, commands, SQL program representation, emitter and compiled SQL |
| `orm/database` | Shared SOCI declarations, numeric conversion, binding payloads, object and result bindings, database and ORM contexts |
| `orm/migrations` | Versioned SQL catalogs, migration runner and project CLI; private database access and SQL validation |
| `reflection` | Fixed strings, compiler signatures, names, aggregate reflection, field metadata and visitation |
| `reflection/generated` | Generated aggregate bindings, grouped by field count |

The small category interfaces such as `orm:model` and `orm:sql` re-export their
leaf partitions. Dependencies flow from reflection and foundation through models,
queries and SQL to database operations. Leaf partitions import the prerequisites
they use; they never import their own category interface. Shared declarations
such as schema traits stay below the units that depend on them, keeping the graph
acyclic. `Database` owns the connection in a separate partition; its context
factory uses the shared forward declaration of `OrmContext`. Both retain their
friendship within the same named module.

The binding partitions re-export their prerequisites along one chain. The
`orm:database_soci` partition owns SOCI's full headers and exposes the original
global-module declarations through using declarations. This avoids duplicate
constructors and conversion specializations when GCC and MSVC import the
bindings through multiple module units.
The three SOCI conversion specializations are defined with `OrmContext`, which
instantiates them, rather than being repeatedly imported through helper partitions.

`tools/generate_reflection_bindings.py` regenerates the checked-in binding
partitions for aggregates with 0–128 fields. It limits each group to at most
eight field counts and a source-size budget. Python and the formatter are needed
only when regenerating these sources, not when building the library.

CMake lists every partition explicitly in its `CXX_MODULES` file sets and
generates `orm:config` and `orm:backend_relations` for the selected backends.
`orm:internal` is an implementation partition used by the library's implementation
units. Applications continue to import only `orm` or `orm.reflection`.

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
their subdirectories under `share/orm-cxx/modules`, and native CMake
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
