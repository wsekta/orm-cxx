# CI Compiler Matrix

This document describes the compiler versions tested in CI and the coverage
policy for the `orm-cxx` project.

## CI Status

| Workflow | Status |
|----------|--------|
| GCC (13, 14) | [![GCC](https://github.com/wsekta/orm-cxx/actions/workflows/linux-gxx-build.yml/badge.svg?branch=main)](https://github.com/wsekta/orm-cxx/actions/workflows/linux-gxx-build.yml) |
| Clang (18–20) | [![Clang](https://github.com/wsekta/orm-cxx/actions/workflows/linux-clang-build.yml/badge.svg?branch=main)](https://github.com/wsekta/orm-cxx/actions/workflows/linux-clang-build.yml) |
| PostgreSQL | [![PostgreSQL](https://github.com/wsekta/orm-cxx/actions/workflows/postgresql-build.yml/badge.svg?branch=main)](https://github.com/wsekta/orm-cxx/actions/workflows/postgresql-build.yml) |
| MSVC | [![MSVC](https://github.com/wsekta/orm-cxx/actions/workflows/windows-msvc-build.yml/badge.svg?branch=main)](https://github.com/wsekta/orm-cxx/actions/workflows/windows-msvc-build.yml) |
| Quality | [![Quality](https://github.com/wsekta/orm-cxx/actions/workflows/quality.yml/badge.svg?branch=main)](https://github.com/wsekta/orm-cxx/actions/workflows/quality.yml) |
| Coverage | [![codecov](https://codecov.io/github/wsekta/orm-cxx/graph/badge.svg?token=MREUNGY5C9)](https://codecov.io/github/wsekta/orm-cxx) |

## Tested Compiler Versions

All Linux CI runs on **Ubuntu 24.04 (Noble)**. GCC and Clang versions are
installed from the default Ubuntu repositories.

### GCC

| Version | Package | Workflow | Status |
|---------|---------|----------|--------|
| GCC 13 | `gcc-13` / `g++-13` | `linux-gxx-build.yml` | Tested |
| GCC 14 | `gcc-14` / `g++-14` | `linux-gxx-build.yml` | Tested |

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
| MSVC latest | `windows-2022` | `windows-msvc-build.yml` | Tested |

## PostgreSQL Compiler Matrix

PostgreSQL integration tests are run with multiple compiler/backend version
combinations:

| Compiler | PostgreSQL | Coverage | Workflow |
|----------|-----------|----------|----------|
| GCC 13 | 15 | No | `postgresql-build.yml` |
| GCC 14 | 15 | No | `postgresql-build.yml` |
| Clang 18 | 18 | Yes | `postgresql-build.yml` |
| Clang 19 | 18 | No | `postgresql-build.yml` |
| Clang 20 | 18 | No | `postgresql-build.yml` |

## Coverage Policy

Code coverage is collected on a **single Clang version only** (Clang 18) to
avoid redundant coverage reports and keep Codecov integration simple:

- **SQLite coverage:** `linux-clang-build.yml` — Clang 18 with `linux-clang-coverage` preset
- **PostgreSQL coverage:** `postgresql-build.yml` — Clang 18 with `linux-clang-postgresql-coverage` preset

Coverage flags uploaded to Codecov:
- `linux-clang-coverage` — SQLite coverage
- `linux-clang-postgresql-coverage` — PostgreSQL coverage

## CMake Presets

Each compiler version has dedicated CMake presets. The existing presets
(`linux-gcc-debug`, `linux-clang-debug`, etc.) remain for backward
compatibility with local development workflows.

| Preset | Compiler | Purpose |
|--------|----------|---------|
| `linux-gcc-debug` | GCC 13 | Local development (default) |
| `linux-gcc-14-debug` | GCC 14 | CI matrix |
| `linux-clang-debug` | Clang 18 | Local development (default) |
| `linux-clang-coverage` | Clang 18 | CI coverage |
| `linux-clang-19-debug` | Clang 19 | CI matrix |
| `linux-clang-19-coverage` | Clang 19 | Local coverage (optional) |
| `linux-clang-19-postgresql` | Clang 19 | PostgreSQL CI matrix |
| `linux-clang-20-debug` | Clang 20 | CI matrix |
| `linux-gcc-postgresql` | GCC 13 | PostgreSQL (default) |
| `linux-gcc-14-postgresql` | GCC 14 | PostgreSQL CI matrix |
| `linux-clang-postgresql-coverage` | Clang 18 | PostgreSQL coverage |
| `linux-clang-19-postgresql-coverage` | Clang 19 | PostgreSQL local (optional) |
| `linux-clang-20-postgresql` | Clang 20 | PostgreSQL CI matrix |
| `msvc-debug` | MSVC | Windows (default) |
| `msvc-postgresql-debug` | MSVC | Windows PostgreSQL |

## Adding New Compiler Versions

To add a new compiler version to CI:

1. Verify the compiler's consteval/constexpr support matches Clang 18+
2. Add new CMake presets in `CMakePresets.json` (configure, build, test, workflow)
3. Add a new job in the relevant workflow in `.github/workflows/`
4. Update this document with the new version
5. If the new version should collect coverage, update the coverage policy section
