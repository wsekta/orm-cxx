# How to contribute

## Getting started

Fork the repository, then create a branch from `main`. Clone with submodules so
the vendored build dependencies are present from the start:

```bash
git clone --recurse-submodules https://github.com/<your-account>/orm-cxx.git
cd orm-cxx
git switch -c feature/short-description
```

If you already cloned the repository without submodules, run:

```bash
git submodule update --init --recursive
```

Keep commits focused, add tests for behavior changes, and include documentation
changes in the same pull request as user-facing changes.

## Fastest supported setup

The default development image contains Clang/LLVM 18, Ninja, CMake, SQLite, PostgreSQL and Oracle MySQL client libraries,
clang-format, clang-tidy, and cmake-format. Docker Compose and the devcontainer
both build that image from the repository's single `Dockerfile`. The separate
`gcc` Compose service uses the official GCC 15 image and the same pinned
CMake and Ninja versions, while preserving the Clang image's standard library.

Run the normal Clang build, tests, and quality checks from a clean checkout:

```bash
docker compose run --build --rm dev bash ./scripts/check-fast.sh
```

Reproduce the non-server Linux build, test, core coverage, and quality
workflows:

```bash
docker compose run --build --rm dev bash ./scripts/check-linux-ci.sh
```

The second command produces the Clang coverage report at
`build/linux-clang-coverage/coverage.lcov`.

To use an editor container instead, open the repository in a client that
supports the [Development Containers specification](https://containers.dev/)
and choose **Reopen in Container**. It uses the same Compose service and image
as the commands above.

The Linux image does not contain or emulate the Microsoft toolchain. Run the
MSVC workflow natively on Windows as described below.

## Presets and CI-equivalent commands

CMake 3.31 or newer is required for the workflow presets. Each workflow
configures, builds, and tests its own directory under `build/`.

| Check | Local command | Output directory |
| --- | --- | --- |
| Fast Linux check | `bash ./scripts/check-fast.sh` | `build/linux-clang-debug`, `build/quality` |
| GCC 15 build and tests | `docker compose run --build --rm gcc cmake --workflow --preset linux-gcc-debug` | `build/linux-gcc-debug` |
| Clang 18 build and tests | `cmake --workflow --preset linux-clang-debug` | `build/linux-clang-debug` |
| Clang 18 coverage | `cmake --workflow --preset linux-clang-coverage` | `build/linux-clang-coverage` |
| Format and static analysis | `bash ./scripts/check-quality.sh` | `build/quality` |
| Clang Linux verification | `bash ./scripts/check-linux-ci.sh` | core Linux directories above |
| MSVC build and tests | `./scripts/check-msvc.ps1` | `build/msvc-debug` |
| PostgreSQL 15 profile | `cmake --workflow --preset linux-gcc-postgresql` | `build/linux-gcc-postgresql` |
| PostgreSQL 18 coverage profile | `cmake --workflow --preset linux-clang-postgresql-coverage` | `build/linux-clang-postgresql-coverage` |

The named presets own compiler paths, build options, warning policy, and
coverage settings. The PostgreSQL profiles require
`ORM_CXX_POSTGRESQL_TEST_DSN` and a live server; the Compose setup is documented
in [Backends](docs/backends.md#postgresql). Do not copy preset flags into local
scripts.

All backend options default to `OFF`. Base debug presets build the core and
core tests without client libraries. Use `sqlite`, `postgresql`, `mysql`,
`mariadb` or `all-backends` presets to select adapters. Server profiles require
their matching `ORM_CXX_<BACKEND>_TEST_DSN` and live services.

Every selected-adapter build compiles the shared examples. `example`,
`relations-example` and `static-queries-example` accept a DSN argument and
create or remove example tables; use a disposable database. The
`backend-connection-example` accepts the same argument and only reports
capabilities. `migrations-example` uses the documented migration CLI.

## Database backend work

SQLite, PostgreSQL, MySQL and MariaDB have equal support contracts. Before changing
backend selection, SQL generation, binding, execution, or driver dependencies,
read the project documentation for [backend
portability](docs/backend-portability.md) and the [backend extension
contract](docs/backend-extension.md). The current support matrix is maintained
in [Backends](docs/backends.md).

Backend extension is currently a source-level contribution model. The project
does not promise a stable binary plugin ABI or compatibility for independently
compiled backend modules. A new backend must keep its driver dependency optional,
declare capabilities and limits centrally, reuse the common conformance tests,
and run those tests against a real database service in CI.

Backend pull requests must update the support matrix and document connection
format, supported server versions, capabilities, limits, and known exclusions.
Raw SQL examples must name the dialect they target; SOCI driver availability by
itself is not a backend support claim.

## Native Linux setup

Install CMake 3.31 or newer, Ninja 1.11 or newer, SQLite development headers, `libpq`
development headers, Oracle MySQL client headers, GCC 15, Clang/LLVM 18, and the
Python tools pinned in `tools/requirements-dev.txt`.
Then invoke the same workflow presets shown above. The container is the
reference environment when host package names or versions differ.

## Native Windows and MSVC

Install Visual Studio 2026 with the **Desktop development with C++** workload,
MSVC 19.50+, CMake 3.31+, Ninja 1.11+, and PowerShell. Initialize the repository submodules, then open a
Visual Studio Developer PowerShell and run:

```powershell
./externals/vcpkg/bootstrap-vcpkg.bat
./externals/vcpkg/vcpkg.exe install
./scripts/check-msvc.ps1
```

The first two commands bootstrap pinned vcpkg and install core dependencies.
Select backend dependencies explicitly with `--x-feature=sqlite`,
`--x-feature=postgresql`, `--x-feature=mysql` and `--x-feature=mariadb`, then use
the corresponding preset, such as `msvc-all-backends-debug`. That profile checks
all 16 consumer subsets, including compile failures for disabled adapters.
The script invokes the core-only `msvc-debug` workflow.

## Documentation

For documentation changes, install Python 3.12+, Doxygen 1.9.8+ and Graphviz, then
run `python scripts/check_docs.py`. It checks local Markdown links and anchors,
quickstart source/output consistency and the generated public API reference.
Generated output is under `build/docs/`; no database clients or C++ build are
needed for that command. The same check runs before documentation publication.
The quickstart's real build and CRUD execution run on GCC, Clang and MSVC in CI.

For Doxygen compatibility, documentation generation unwraps module export blocks
and preserves requires-clauses as comments in a generated source view. The
original module sources remain the authority for compile-time constraints;
the model and query guides explain their user-visible rules. This generated view
is never compiled or installed as a header interface.

## Build configuration and macro policy

Backend options are selected when building the library or package. The `orm`
module exports `orm::config::sqliteBackendEnabled` and
`orm::config::postgresqlBackendEnabled`, `orm::config::mysqlBackendEnabled`
and `orm::config::mariadbBackendEnabled` as C++ constants. Use `if constexpr`
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

## Before submitting a pull request

- Run the fast check while iterating and the relevant full workflows before
  pushing.
- Add or update tests for changed behavior.
- Keep generated build, coverage, database, and formatting artifacts out of
  the commit.
- Run `bash ./scripts/format.sh` to apply the pinned C++ and CMake formatters,
  then let the checks validate the result. The repository `.clang-format` and
  `.cmake-format.yaml` files are the source of formatting policy.
- Push the branch to your fork and open a pull request against `main`.
