# Package managers and releases

`VERSION.txt` is the release version shared by CMake, vcpkg, and Conan. The initial
version is `0.1.0`; use a stable `major.minor.patch` value and increase it before
publishing changed sources. The library currently ships as a static library.

The **Packages** workflow validates packages on `main`, pull requests, and
`release`. A successful push to `release` also publishes a GitHub source release
and opens pull requests against the **official** `microsoft/vcpkg` and
`conan-io/conan-center-index` repositories. Their maintainers review the requests;
their infrastructure makes an accepted package available in the central catalog.
An upstream GitHub release or a successful submission job does **not** mean the
package is already available there. Until the initial requests are accepted,
the central installation examples below will not resolve `orm-cxx`.

No private registry or Conan remote is required. SOCI and enabled database client
libraries are dependencies of the package and are installed by the manager.
Consumers still need a supported C++20 compiler, CMake, and the selected package
manager. On a minimal Linux host, vcpkg also needs its ordinary host tool
`pkg-config`; the Packages workflow installs it. No manual SOCI/SQLite/libpq
development-package installation is needed.
SQLite works in process; PostgreSQL applications still need a database server
to connect to (the package supplies the client library, not a running server).

## Consume from vcpkg after acceptance

Add an `orm-cxx` dependency to the application's `vcpkg.json`:

```json
{
  "dependencies": ["orm-cxx"]
}
```

This enables SQLite. For both backends, use
`{"name": "orm-cxx", "features": ["postgresql"]}`. For PostgreSQL alone, use
`{"name": "orm-cxx", "default-features": false, "features": ["postgresql"]}`.
For the core without database drivers, disable default features and omit
`features`. Pin a vcpkg `builtin-baseline` containing the accepted port for
reproducible builds; a baseline predating the initial submission cannot resolve it.

```cmake
cmake_minimum_required(VERSION 3.22)
project(application LANGUAGES CXX)
find_package(orm-cxx CONFIG REQUIRED)
add_executable(application main.cpp)
target_link_libraries(application PRIVATE orm-cxx::orm-cxx)
```

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

The port always builds the `orm-cxx` static library. `x64-windows` is supported
and uses the dynamic Microsoft CRT selected by that triplet. Select
`x64-windows-static` when the application and its dependencies must use the
static Microsoft CRT:

```powershell
vcpkg install --triplet x64-windows-static
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:\path\to\vcpkg\scripts\buildsystems\vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static
```

On Linux the default `x64-linux` triplet is suitable.

The imported target propagates headers, C++20, enabled-backend definitions, and
transitive linkage. `find_package(orm-cxx-reflection CONFIG REQUIRED)` exposes
`orm-cxx::reflection` for applications using only the header-only reflection layer.

## Consume from ConanCenter after acceptance

Use Conan 2 and a `conanfile.txt`:

```ini
[requires]
orm-cxx/0.1.0

[generators]
CMakeDeps
CMakeToolchain
```

```sh
conan profile detect
conan install . --output-folder=build --build=missing -s compiler.cppstd=20 -s build_type=Release
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Use the same `find_package(orm-cxx)` and target as above and explicitly set
`target_compile_features(application PRIVATE cxx_std_20)` with Conan's generated
CMake config. The generated config also exposes `orm-cxx::reflection`.
Conan options are `orm-cxx/*:with_sqlite3=True` (default) and
`orm-cxx/*:with_postgresql=False` (default). Pass `-o` to `conan install` to select
the backends. ConanCenter supplies available binaries and `--build=missing`
builds configurations that do not have a matching binary.

## Maintainer setup and publishing

1. Add the repository Actions secret **`PACKAGE_REGISTRY_TOKEN`**, containing a
   GitHub personal access token for an account allowed to fork the public
   catalogs, push recipe branches to its forks, and open upstream PRs. A classic
   token with `public_repo` scope is suitable for these public repositories.
   The repository's ordinary `GITHUB_TOKEN` cannot perform these cross-repository
   operations. The script creates the forks automatically. Do not put tokens in
   files, recipes, workflow inputs, or commit messages.
2. Before the first ConanCenter submission, the account behind that token must
   sign the ConanCenter Index CLA. Open or identify the ConanCenter issue for
   the new package, then set the repository Actions variable
   **`CONANCENTER_ISSUE`** to its positive numeric identifier. The pipeline
   adds `Fixes #<issue>` to the ConanCenter PR and stops before release if the
   variable is absent or malformed. Update the variable when a later release
   needs a different related issue.
3. Give GitHub Actions permission to write repository contents (the release job
   requests `contents: write`). Branch protection rules and organization token
   policies may require an administrator's configuration.
4. Update `VERSION.txt`, review the changes on `main`, and wait for its checks. Create
   the `release` branch from the reviewed commit, or fast-forward the existing
   branch to it. For example, for the **first** release only:

   ```sh
   git push origin main:release
   ```

5. The workflow builds the archive once, checks it through both managers on Linux
   and Windows, then creates tag `v<version>` and a GitHub release with the source
   archive, `SHA256SUMS`, and provenance metadata `release.json`.
6. Separate submission jobs open/update version-specific PRs in both catalogs.
   Track their review and central CI until accepted. vcpkg version database
   entries are generated with `x-add-version`; Conan `config.yml` and
   `conandata.yml` retain existing versions. No binary is uploaded directly to
   ConanCenter by this repository; ConanCenter builds its own binaries.

Release jobs only run for the `release` branch. PR builds receive no publication
credentials. A manual workflow dispatch on `release` retries the process; existing
tags and source assets must still match the exact tested commit and bytes. Pushes
are never forced, open PRs are reused, and a closed unmerged submission requires
maintainer action. Increase `VERSION.txt` when changing a published release. If a
network failure interrupts an unpublished draft, rerunning completes missing
assets after checking the assets already present. A missing token stops publishing
with an actionable error instead of reporting false success.

## Local package verification

Install the packaging tools into a Python virtual environment:

```sh
python -m pip install -r tools/requirements-packaging.txt
python -m unittest discover -s tests/packaging -v
python scripts/prepare_packages.py
python scripts/check_packages.py --manager conan --backends sqlite
git submodule update --init externals/vcpkg
./externals/vcpkg/bootstrap-vcpkg.sh -disableMetrics
python scripts/check_packages.py --manager vcpkg --backends sqlite
```

On Windows, run in a Visual Studio developer shell and bootstrap with
`externals\vcpkg\bootstrap-vcpkg.bat`. The default validation triplet is
`x64-windows-static` on Windows and `x64-linux` on Linux. `x64-windows` is also
supported and produces a static `orm-cxx` library with the triplet's dynamic CRT;
use `x64-windows-static` when validating a fully static CRT configuration.
`--backends` accepts `sqlite`,
`postgresql`, `both`, and `core`. Use a separate `--work` directory for each
concurrent invocation. The Conan check defaults `CONAN_HOME` to
`build/conan-home` and detects a profile there. To use an existing profile, pass
`--conan-profile=PROFILE`. `--conan-setting` and `--conan-conf` are repeatable
overrides (for example, `--conan-setting=compiler.version=193` when multiple
Visual Studio versions are installed). Explicit `CONAN_HOME` values are honored.

The scripts generate an archive containing only this project's library sources
and metadata, normalizing CRLF to LF so its bytes and checksums are stable across
Windows and Linux checkouts. A temporary loopback HTTP server supplies that same archive to the
actual recipes, including their checksum verification. Consumers build without
the source tree or bundled dependency submodules. SQLite checks execute CRUD;
PostgreSQL checks verify linking and backend registration without requiring a
server. Existing backend integration workflows provide live database coverage.
Linux tests cover all four backend selections; Windows tests cover SQLite and
the combined backends. Recipes under `packaging/` are templates: the prepared
submission under `build/distribution` contains the release version, URL, and hashes.

## Native CMake installation

Package recipes set `ORM_CXX_USE_SYSTEM_SOCI=ON`, disable developer tests and
examples, then use `cmake --install`. An existing SOCI CMake package is required
in this mode. Installed configs provide `orm-cxx::orm-cxx` and
`orm-cxx::reflection`; full-library components are `core`, `reflection`,
`sqlite3`, and `postgresql`. Requesting a disabled backend as a required CMake
component fails at configuration time. Compatibility is limited to the same
minor release series while the library is at version `0.x`.

The normal repository/submodule build remains available for development.

References: [vcpkg central submissions](https://learn.microsoft.com/en-us/vcpkg/get_started/get-started-adding-to-registry),
[ConanCenter contribution guide](https://github.com/conan-io/conan-center-index/blob/master/CONTRIBUTING.md).
