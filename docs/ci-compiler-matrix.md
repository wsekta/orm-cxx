# CI Compiler Matrix

This document describes the compiler versions tested in CI and the coverage
policy for the `orm-cxx` project.

## CI Status

| Workflow | Status |
|----------|--------|
| GCC 15 | [![GCC](https://github.com/wsekta/orm-cxx/actions/workflows/linux-gxx-build.yml/badge.svg?branch=main)](https://github.com/wsekta/orm-cxx/actions/workflows/linux-gxx-build.yml) |
| Clang (18–20) | [![Clang](https://github.com/wsekta/orm-cxx/actions/workflows/linux-clang-build.yml/badge.svg?branch=main)](https://github.com/wsekta/orm-cxx/actions/workflows/linux-clang-build.yml) |
| Backend conformance | [![Backend conformance](https://github.com/wsekta/orm-cxx/actions/workflows/postgresql-build.yml/badge.svg?branch=main)](https://github.com/wsekta/orm-cxx/actions/workflows/postgresql-build.yml) |
| MSVC | [![MSVC](https://github.com/wsekta/orm-cxx/actions/workflows/windows-msvc-build.yml/badge.svg?branch=main)](https://github.com/wsekta/orm-cxx/actions/workflows/windows-msvc-build.yml) |
| Quality | [![Quality](https://github.com/wsekta/orm-cxx/actions/workflows/quality.yml/badge.svg?branch=main)](https://github.com/wsekta/orm-cxx/actions/workflows/quality.yml) |
| Coverage | [![codecov](https://codecov.io/github/wsekta/orm-cxx/graph/badge.svg?token=MREUNGY5C9)](https://codecov.io/github/wsekta/orm-cxx) |

## Tested Compiler Versions

Linux CI uses **Ubuntu 24.04 (Noble)** runners. GCC jobs run inside the
official `gcc:15-trixie` image; Clang jobs install compiler packages from
the Ubuntu repositories. Linux package-manager jobs use Clang 18.

### GCC

| Version | Package | Workflow | Status |
|---------|---------|----------|--------|
| GCC 15 | `gcc-15` / `g++-15` | `linux-gxx-build.yml` | Tested |

GCC 15.3.0 and 16.2.0 were validated with the full module build. GCC 14
exceeds its imported source-location limit. GCC 15 and 16 require
`-fno-module-lazy` on both module producers and importers to avoid
`Bad file data` from the lazy reader; both CMake targets propagate this
option, including installed interfaces rebuilt by consumers.

The GCC workflow also installs ORM and SOCI into an isolated prefix and builds
the `find_package` consumer with warnings treated as errors. A separate installed
reflection consumer disables SOCI discovery. Both checks rebuild their module
interfaces using the targets' public options and then run the resulting programs.

### Clang

| Version | Package | Workflow | Coverage | Status |
|---------|---------|----------|----------|--------|
| Clang 18 | `clang-18` / `clang++-18` | `linux-clang-build.yml` | Yes | Tested |
| Clang 19 | `clang-19` / `clang++-19` | `linux-clang-build.yml` | No | Tested |
| Clang 20 | `clang-20` / `clang++-20` | `linux-clang-build.yml` | No | Tested |

> **Note:** Clang 16 and 17 are not supported. The compile-time reflection
> layer uses `consteval` patterns that require Clang 18 or newer.

### MSVC

| Version | Runner | Workflow | Status |
|---------|--------|----------|--------|
| MSVC latest | `windows-2025-vs2026` | `windows-msvc-build.yml` | Tested |

## Backend and consumer matrix

`postgresql-build.yml` runs all four adapters through the same conformance
suite. The workflow name is **Backend conformance**. Clang 18, 19 and 20 and
GCC 15 cover PostgreSQL 15/18, MySQL 8.4/9.7 and MariaDB 11.4/11.8/12.3.
MSVC builds every adapter; live server tests run on Linux.
Each all-adapter build registers consumers for all 16 independent subsets.
Every consumer checks exact registry membership and compiles negative controls
for disabled provider types. Core compiler jobs require no database clients.

The Packages workflow repeats the 16 subsets for Conan and vcpkg on Linux and
Windows. Linux installed consumers run live CRUD and migrations against every
selected service; Windows verifies module, provider and linking contracts.

## Coverage policy

Clang 18 produces five independent reports: core, SQLite, PostgreSQL, MySQL and
MariaDB. The backend conformance workflow merges the full report list and
requires 100% line coverage. Every adapter contributes under the same policy.

The LLVM exporter removes compiler-synthesized implicit-member maps only when
every zero-count region exactly covers a class name on a plain `class` or
`struct` declaration line in a project module, and LLVM profdata has no
instrumentation record for that function. It verifies that all profile records
were read. Explicit method bodies and instrumented functions with zero hits
remain coverage failures. The exporter reports how many maps and declaration
lines it normalized. Python is required only for coverage builds.

The merged report is uploaded with Codecov flag `all-backends`; the standalone
core compiler workflow also uploads its core report.

## CMake Presets

Each compiler version has dedicated CMake presets. All library and consumer
builds use native C++20 modules through Ninja 1.11+ and
CMake 3.31+. CI installs CMake 3.31.6 and Ninja 1.13.0 explicitly. The default
GCC presets select GCC 15; Clang 18 remains the coverage compiler.

| Preset | Compiler | Purpose |
|--------|----------|---------|
| `linux-gcc-debug` | GCC 15 | Local development (default) |
| `linux-gcc-15-debug` | GCC 15 | CI matrix |
| `linux-clang-debug` | Clang 18 | Local development (default) |
| `linux-clang-coverage` | Clang 18 | Core coverage |
| `linux-clang-19-debug` | Clang 19 | CI matrix |
| `linux-clang-19-coverage` | Clang 19 | Local coverage (optional) |
| `linux-clang-19-postgresql` | Clang 19 | PostgreSQL CI matrix |
| `linux-clang-20-debug` | Clang 20 | CI matrix |
| `linux-gcc-postgresql` | GCC 15 | PostgreSQL (default) |
| `linux-gcc-15-postgresql` | GCC 15 | PostgreSQL CI matrix |
| `linux-clang-postgresql-coverage` | Clang 18 | PostgreSQL coverage |
| `linux-clang-19-postgresql-coverage` | Clang 19 | PostgreSQL local (optional) |
| `linux-clang-20-postgresql` | Clang 20 | PostgreSQL CI matrix |
| `msvc-debug` | MSVC | Windows (default) |
| `msvc-postgresql-debug` | MSVC | Windows PostgreSQL |
| `msvc-all-backends-debug` | MSVC | All adapters and 16 consumers |
| `linux-clang-all-backends` | Clang 18 | Live conformance and 16 consumers |
| `linux-gcc-15-all-backends` | GCC 15 | Live conformance and 16 consumers |

Single-backend debug presets exist for `sqlite`, `postgresql`, `mysql` and
`mariadb` for each supported compiler. Clang 18 coverage presets follow
`linux-clang-<backend>-coverage`. Base debug and coverage presets select core;
all four backend options remain disabled unless a profile selects them.

## Adding New Compiler Versions

To add a new compiler version to CI:

1. Verify module scanning, module ownership, and consteval/constexpr support
2. Build and install both modules, then run a relocated installed consumer
3. Add new CMake presets in `CMakePresets.json` (configure, build, test, workflow)
4. Add a new job in the relevant workflow in `.github/workflows/`
5. Update this document with the new version
6. If the new version should collect coverage, update the coverage policy section
