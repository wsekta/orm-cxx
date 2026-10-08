# Backend configuration without macros

ORM C++ defines no preprocessor macros in its library, test support, or exported
package metadata. The migration removes all 14 project-defined macro names:
operator-generation helpers, the unused warning helpers, and backend/test flags.
Model declarations and the typed query syntax remain unchanged.

## Read the configuration in C++

The selected build or package provides `orm-cxx/BuildConfig.hpp`. It is also
included by `orm-cxx/database.hpp` and the umbrella `orm-cxx/orm.hpp`.

```cpp
#include <orm-cxx/BuildConfig.hpp>

static_assert(orm::config::sqliteBackendEnabled);

int main()
{
    if constexpr (orm::config::postgresqlBackendEnabled)
    {
        // Use PostgreSQL-specific functionality here.
    }
}
```

Replace `ORM_CXX_ENABLE_SQLITE_BACKEND` with
`orm::config::sqliteBackendEnabled`, and `ORM_CXX_ENABLE_POSTGRESQL_BACKEND` with
`orm::config::postgresqlBackendEnabled`. Both are `inline constexpr bool` values
that describe the library being linked. They cannot be overridden by defining a
macro in a consumer.

Replace preprocessor conditionals with C++ constant expressions. For code that
must compile with a disabled backend, keep a backend-dependent expression in a
template when its validity depends on the condition: a non-template
`if constexpr` still requires both branches to be well-formed.

The names and defaults of the CMake options are unchanged:

```sh
cmake -S . -B build -DORM_CXX_ENABLE_SQLITE_BACKEND=ON -DORM_CXX_ENABLE_POSTGRESQL_BACKEND=OFF
```

These `-D` arguments set CMake cache options; they are not compiler definitions.
Conan's `with_sqlite3` and `with_postgresql` options and vcpkg backend features
also retain their names and defaults. Link the provided CMake target to obtain
the generated header from a source build or an installed package. The standalone
reflection target remains independent of backend configuration.

## Compatibility changes

There are no legacy macro aliases. Code testing the earlier backend macros must
migrate to the constants above. The default-dialect SQLite overloads remain
available only when SQLite is enabled, with their existing signatures. Disabled
drivers are not required for linking.

`orm-cxx/utils/DisableExternalsWarning.hpp` has been removed. It was not used by
the library; consumers that included it must remove the include and configure
warning handling through their own build system.

The internal PostgreSQL integration flag and package-consumer expectation flags
are private generated C++ constants. They are not part of the public API. The
uppercase `ORM_QUERY_*` concepts and diagnostic identifiers are C++ names or
messages, not macros, and remain unchanged.

## Policy and checks

`python scripts/check-macros.py` checks first-party C/C++ sources, header
templates, CMake configuration, and Conan recipes from Git's inventory. It also
checks new, unignored files before they are committed. The Quality workflow and
`scripts/check-quality.sh` run this check and its regression tests.

Project-owned `#define`, `#undef`, and `#cmakedefine` directives and exported
compiler definitions are forbidden. Backend selection belongs in generated C++
constants, source selection, and dependency linkage. The checker permits CMake
cache `-D` options and the include-only arguments passed through
`try_compile(... COMPILE_DEFINITIONS ...)`.

External dependencies are excluded from this policy. Tests may use
GoogleTest/GoogleMock macros, and SQLite adapters may use SQLite's constants
such as `SQLITE_CONSTRAINT`. Compiler conditionals are limited to existing
reflection adapters and platform-dependent tests: `_MSC_VER`, `__clang__`,
`__GNUC__`, and `__SIZEOF_INT128__`. New compiler exceptions require an explicit
update to the checker's path-specific allowlist. Build directories and vendored
third-party CMake modules are excluded from the inventory.
