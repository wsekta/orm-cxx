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

The development image contains GCC 13, Clang/LLVM 18, Ninja, CMake, SQLite, PostgreSQL client libraries,
clang-format, clang-tidy, and cmake-format. Docker Compose and the devcontainer
both build that image from the repository's single `Dockerfile`.

Run the normal Clang build, tests, and quality checks from a clean checkout:

```bash
docker compose run --build --rm dev bash ./scripts/check-fast.sh
```

Reproduce all supported Linux build, test, coverage, and quality workflows:

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

CMake 3.25 or newer is required for the workflow presets. Each workflow
configures, builds, and tests its own directory under `build/`.

| Check | Local command | Output directory |
| --- | --- | --- |
| Fast Linux check | `bash ./scripts/check-fast.sh` | `build/linux-clang-debug`, `build/quality` |
| GCC 13 build and tests | `cmake --workflow --preset linux-gcc-debug` | `build/linux-gcc-debug` |
| Clang 18 build and tests | `cmake --workflow --preset linux-clang-debug` | `build/linux-clang-debug` |
| Clang 18 coverage | `cmake --workflow --preset linux-clang-coverage` | `build/linux-clang-coverage` |
| Format and static analysis | `bash ./scripts/check-quality.sh` | `build/quality` |
| Full Linux verification | `bash ./scripts/check-linux-ci.sh` | all Linux directories above |
| MSVC build and tests | `./scripts/check-msvc.ps1` | `build/msvc-debug` |
| PostgreSQL 15/18 profile | `cmake --workflow --preset linux-gcc-postgresql` or `linux-clang-postgresql-coverage` | matching preset directory |

The named presets own compiler paths, build options, warning policy, and
coverage settings. Do not copy those flags into local scripts.

Top-level debug presets build both example executables as well as the tests.
The verification scripts compile the examples but intentionally do not run
them, because the examples create SQLite files in their current directory. If
you run one manually, use a temporary working directory so the source checkout
stays clean:

```bash
repo_root="$PWD"
example_dir="$(mktemp -d)"
(cd "$example_dir" && "$repo_root/build/linux-clang-debug/examples/example")
rm -rf "$example_dir"
```

Run `relations-example` the same way if needed.

## Database backend work

SQLite is the default backend and PostgreSQL is optional. Before changing
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

Install CMake 3.25 or newer, Ninja, SQLite development headers, `libpq`
development headers, the PostgreSQL client, GCC 13, Clang/LLVM 18, and the
Python tools pinned in `tools/requirements-dev.txt`.
Then invoke the same workflow presets shown above. The container is the
reference environment when host package names or versions differ.

## Native Windows and MSVC

Install Visual Studio 2022 with the **Desktop development with C++** workload,
CMake, Ninja, and PowerShell. Initialize the repository submodules, then open a
Visual Studio Developer PowerShell and run:

```powershell
./externals/vcpkg/bootstrap-vcpkg.bat
./externals/vcpkg/vcpkg.exe install
./scripts/check-msvc.ps1
```

The first two commands bootstrap the repository's pinned vcpkg checkout and
install SQLite. Add `--x-feature=postgresql` to the install command and use the
`msvc-postgresql-debug` workflow preset to compile the PostgreSQL adapter and
all consumer configurations. They are required once per clean checkout. The script itself
only invokes the `msvc-debug` workflow preset; the preset contains the
toolchain and build settings.

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
