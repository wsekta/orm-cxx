# Package managers and releases

`VERSION.txt` is the release version shared by CMake, vcpkg, and Conan. The current
version is `0.4.0`; use a stable `major.minor.patch` value and increase it before
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
Consumers require GCC 14+, Clang 18+, or MSVC 19.50+, CMake 3.31+, Ninja or
Ninja Multi-Config 1.11+, and the selected package manager. Standard-library headers
are included normally; header units and `import std` are not required.
On a minimal Linux host, vcpkg also needs its ordinary host tool
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
cmake_minimum_required(VERSION 3.31)
project(application LANGUAGES CXX)
find_package(orm-cxx CONFIG REQUIRED)
add_executable(application main.cpp)
target_link_libraries(application PRIVATE orm-cxx::orm-cxx)
```

```sh
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

The port always builds the `orm-cxx` static library. `x64-windows` is supported
and uses the dynamic Microsoft CRT selected by that triplet. Select
`x64-windows-static` when the application and its dependencies must use the
static Microsoft CRT:

```powershell
vcpkg install --triplet x64-windows-static
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=C:\path\to\vcpkg\scripts\buildsystems\vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static
```

On Linux the default `x64-linux` triplet is suitable.

The imported target propagates C++20, module source file sets, import metadata,
and transitive linkage. Applications write `import orm;`; the module exposes the
enabled backends as `orm::config::sqliteBackendEnabled` and
`orm::config::postgresqlBackendEnabled`. No compiler macro definitions are exported.
`find_package(orm-cxx-reflection CONFIG REQUIRED)` exposes the compiled
`orm-cxx::reflection` target for `import orm.reflection;` without SOCI dependencies.

## Consume from ConanCenter after acceptance

Use Conan 2 and a `conanfile.txt`:

```ini
[requires]
orm-cxx/0.4.0

[generators]
CMakeDeps
CMakeToolchain
```

```sh
conan profile detect
conan install . --output-folder=build --build=missing -s compiler.cppstd=20 -s build_type=Release
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Use the same `find_package(orm-cxx)` and target as above. Conan preserves the
library's native CMake configs; CMakeDeps supplies the dependency configs and
skips generating an orm-cxx replacement. The native config also exposes
`orm-cxx::reflection`.
Conan options are `orm-cxx/*:with_sqlite3=True` (default) and
`orm-cxx/*:with_postgresql=False` (default). Pass `-o` to `conan install` to select
the backends. ConanCenter supplies available binaries and `--build=missing`
builds configurations that do not have a matching binary.

## Maintainer setup and publishing

1. Authenticate GitHub CLI as **`wsekta`**, preserving SSH for Git operations:

   ```sh
   gh auth login --hostname github.com --git-protocol ssh --web --skip-ssh-key
   gh api user --jq .login
   ```

   The second command must print `wsekta`. Add the repository Actions secret
   **`PACKAGE_REGISTRY_TOKEN`**, containing a classic GitHub personal access token
   created by `wsekta`, with **`public_repo`** scope and a **30-day expiry**.
   It must allow forking the public catalogs, pushing recipe branches to its forks,
   and opening upstream PRs. Use the GitHub secret input panel to store its value.
   The repository's ordinary `GITHUB_TOKEN` cannot perform these cross-repository
   operations. The script creates the forks automatically. Do not put tokens in
   files, recipes, workflow inputs, or commit messages.
2. Before the first ConanCenter submission, `wsekta` must personally review and
   sign the [ConanCenter Index CLA](https://cla-assistant.io/conan-io/conan-center-index).
   The vcpkg submission may also request the
   [Microsoft CLA](https://github.com/microsoft/vcpkg/blob/master/CONTRIBUTING.md#legal)
   after opening its PR; the account holder must review and sign it.
   Open or identify the ConanCenter issue for the new package, then set the
   repository Actions variable **`CONANCENTER_ISSUE`** to its positive numeric
   identifier. This must identify an existing issue in
   `conan-io/conan-center-index`, not a pull request. The pipeline adds
   `Fixes #<issue>` to the ConanCenter PR. Update the variable when a later release
   needs a different related issue. CLA acceptance and token creation require
   the account holder's interaction.
3. Give GitHub Actions permission to write repository contents (the release job
   requests `contents: write`). Branch protection rules and organization token
   policies may require an administrator's configuration.
4. Use `VERSION.txt` as the sole release version. The current module API
   uses `0.4.0`; increase the minor version for incompatible changes while on `0.x`. For a later release,
   increase it before preparing changed sources. Review the changes on `main`
   and wait for all its checks. Create `release` from that exact reviewed SHA,
   or fast-forward the existing branch to it. For example, for the **first**
   release only, substitute the approved commit SHA:

   ```sh
   git push origin <reviewed-sha>:refs/heads/release
   ```

5. The workflow runs the read-only
   `python scripts/publish_packages.py preflight` before preparing release
   sources and again before publishing them. With `GH_TOKEN` set to the registry
   secret, it verifies the token owner is `wsekta` and the ConanCenter issue
   exists. `PACKAGE_REGISTRY_OWNER` supplies the expected login; its default is
   `wsekta`, and the workflow explicitly sets it. Missing or invalid credentials,
   a different account, and an absent, malformed, or PR issue stop publication.
   Preflight does not create forks, tags, releases, or submissions.
   The workflow builds the archive once, checks it through both managers on Linux
   and Windows, then creates tag `v<version>` and a GitHub release with the source
   archive, `SHA256SUMS`, and provenance metadata `release.json`. Publishing the
   release uses the repository's `GITHUB_TOKEN`; catalog submissions use the
   registry token.
6. Separate submission jobs open/update version-specific PRs in both catalogs.
   Their commits use the authenticated login for the author and committer, with
   `<account-id>+wsekta@users.noreply.github.com` derived from GitHub's account API.
   Track their review and central CI until accepted. vcpkg version database
   entries are generated with `x-add-version`; Conan `config.yml` and
   `conandata.yml` retain existing versions. No binary is uploaded directly to
   ConanCenter by this repository; ConanCenter builds its own binaries.

Release jobs only run for the `release` branch. PR builds receive no publication
credentials. Published tags, commits, and source assets are immutable: the script
requires the exact tested commit and bytes, and refuses a conflicting tag or hash.
Pushes are never forced, open PRs are reused, and a closed unmerged submission
requires maintainer action. Increase `VERSION.txt` when changing published sources.
If a network failure interrupts an unpublished draft, rerunning completes missing
assets after checking the assets already present. A missing token stops publishing
with an actionable error instead of reporting false success.

### Retry without changing the release

For a configuration or transient failure, correct the configuration and rerun
failed jobs for the same release run and SHA. A manual workflow dispatch on
`release` is also safe while the branch still points to that SHA.

If only the submission script needs fixing after publication, commit the fix on
`main`, then run only the affected catalog submission with the updated script and
the **original** `package-distribution` artifact, including its original
`release.json`, archive, checksums, and generated recipes. Download it from the
successful release run and choose a new `--work` directory for each retry:

```sh
gh run download <release-run-id> --name package-distribution --dir build/retry-distribution
python scripts/publish_packages.py vcpkg --distribution build/retry-distribution --work build/retry-vcpkg-0.4.0
python scripts/publish_packages.py conan --distribution build/retry-distribution --work build/retry-conan-0.4.0 --conan-issue <issue-number>
```

Authenticate these local commands as `wsekta`, or provide `GH_TOKEN` for that
account. `--expected-owner wsekta` can explicitly select the same owner check.
Do not rerun `prepare_packages.py` from the newer `main`, move the published tag,
or run the complete release workflow from a moved `release` branch with the same
version. A conflict in the immutable sources must be resolved before proceeding.

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
overrides (for example, `--conan-setting=compiler.version=195` when multiple
Visual Studio versions are installed). Explicit `CONAN_HOME` values are honored.

The scripts generate an archive containing only this project's library sources
and metadata, normalizing CRLF to LF so its bytes and checksums are stable across
Windows and Linux checkouts. A temporary loopback HTTP server supplies that same archive to the
actual recipes, including their checksum verification. Consumers build without
the source tree or bundled dependency submodules. Every configuration builds
typed `Query`, `Update`, and `ProjectionQuery` expressions, including core-only
and PostgreSQL-only packages. Consumer targets enable strict warnings and
warnings-as-errors privately; those flags are not exported to applications.
SQLite checks execute CRUD and DTO projections;
PostgreSQL checks verify linking and backend registration without requiring a
server. Existing backend integration workflows provide live database coverage.
Linux and Windows tests cover all four backend selections. Windows vcpkg
additionally verifies SQLite and both backends with the dynamic CRT triplet. Recipes under `packaging/` are templates: the prepared
submission under `build/distribution` contains the release version, URL, and hashes.

## Native CMake installation

Package recipes set `ORM_CXX_USE_SYSTEM_SOCI=ON`, disable developer tests and
examples, then use `cmake --install`. An existing SOCI CMake package is required
in this mode. Installed configs provide `orm-cxx::orm-cxx` and
`orm-cxx::reflection`; full-library components are `core`, `reflection`,
`sqlite3`, and `postgresql`. Requesting a disabled backend as a required CMake
component fails at configuration time. Compatibility is limited to the same
minor release series while the library is at version `0.x`.

Packages install `.cppm` interface sources under `share/orm-cxx/modules` and
CMake export metadata alongside their native configs. CMake generates BMIs in
the consumer's build tree using that consumer's compiler. The package does not
ship prebuilt BMIs as a portable API, and the static libraries still require a
compatible compiler, standard library, CRT, and ABI configuration. Use the same
C++20 standard and build type for package creation and consumption.

Both modules are the sole public API; public ORM headers are not installed.
The normal repository/submodule build uses the same module targets.

References: [GitHub CLI authentication](https://cli.github.com/manual/gh_auth_login),
[GitHub token scopes](https://docs.github.com/en/apps/oauth-apps/building-oauth-apps/scopes-for-oauth-apps),
[vcpkg central submissions](https://learn.microsoft.com/en-us/vcpkg/get_started/get-started-adding-to-registry),
[ConanCenter contribution guide](https://github.com/conan-io/conan-center-index/blob/master/CONTRIBUTING.md).
