module;

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

module orm;

import :migrations_internal;

namespace orm::migrations
{
namespace
{
using Json = nlohmann::json;
using Path = std::filesystem::path;

auto failure(std::string message) -> void
{
    throw MigrationError{ErrorCode::InvalidCatalog, std::move(message)};
}

auto safeProjectName(std::string_view name) -> bool
{
    return !name.empty() && std::ranges::all_of(name,
                                                [](char character)
                                                {
                                                    return (character >= 'a' && character <= 'z') ||
                                                           (character >= 'A' && character <= 'Z') ||
                                                           (character >= '0' && character <= '9') || character == '_' ||
                                                           character == '-';
                                                });
}

auto regularFile(const Path& path) -> void
{
    if (std::filesystem::is_symlink(path) || !std::filesystem::is_regular_file(path))
    {
        failure("Migration metadata and SQL must be regular files");
    }
}

auto readFile(const Path& path) -> std::string
{
    regularFile(path);
    try
    {
        std::ifstream input{path, std::ios::binary | std::ios::ate};
        input.exceptions(std::ios::failbit | std::ios::badbit);
        const auto size = static_cast<std::streamsize>(input.tellg());
        std::string value(static_cast<std::size_t>(size), '\0');
        input.seekg(0);
        input.read(value.data(), size);
        return value;
    }
    catch (const std::ios_base::failure&)
    {
        failure("Cannot read migration project file");
    }
    return {};
}

auto writeFile(const Path& path, std::string_view value) -> void
{
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    output.exceptions(std::ios::failbit | std::ios::badbit);
    output.write(value.data(), static_cast<std::streamsize>(value.size()));
    output.close();
}

auto readJson(const Path& path) -> Json
{
    try
    {
        std::map<int, std::set<std::string>> keys;
        return Json::parse(readFile(path),
                           [&](int depth, Json::parse_event_t event, Json& value)
                           {
                               if (event == Json::parse_event_t::object_start)
                                   keys[depth + 1].clear();
                               else if (event == Json::parse_event_t::object_end)
                                   keys.erase(depth + 1);
                               else if (event == Json::parse_event_t::key &&
                                        !keys[depth].insert(value.get<std::string>()).second)
                               {
                                   failure("Duplicate migration project JSON field");
                               }
                               return true;
                           });
    }
    catch (const Json::exception&)
    {
        failure("Invalid migration project JSON");
    }
    return {};
}

auto fields(const Json& item, std::initializer_list<std::string_view> names) -> void
{
    if (!item.is_object() || item.size() != names.size())
        failure("Invalid migration project metadata fields");
    for (const auto name : names)
    {
        if (!item.contains(std::string{name}))
            failure("Missing migration project metadata field");
    }
}

auto backendFromName(std::string_view name) -> db::BackendType
{
    for (const auto backend : generationBackends)
    {
        if (detail::backendName(backend) == name)
            return backend;
    }
    failure("Unsupported project backend");
    return db::BackendType::Empty;
}

auto backendNames(std::span<const db::BackendType> backends) -> std::vector<std::string>
{
    std::vector<std::string> names;
    for (const auto backend : backends)
        names.emplace_back(detail::backendName(backend));
    std::ranges::sort(names);
    if (names.empty() || std::adjacent_find(names.begin(), names.end()) != names.end())
    {
        failure("Project requires a nonempty unique backend set");
    }
    return names;
}

class ProjectLock final
{
public:
    explicit ProjectLock(const Path& root) : path_{root / ".orm-cxx-lock"}
    {
        if (!std::filesystem::create_directory(path_))
        {
            throw MigrationError{ErrorCode::LockUnavailable, "Another migration project writer holds the lock"};
        }
    }
    ProjectLock(const ProjectLock&) = delete;
    auto operator=(const ProjectLock&) -> ProjectLock& = delete;
    ~ProjectLock()
    {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

private:
    Path path_;
};

class StagingDirectory final
{
public:
    explicit StagingDirectory(const Path& root) : path{root / ".orm-cxx-staging"}
    {
        if (!std::filesystem::create_directory(path))
            failure("A previous migration staging directory exists");
    }
    StagingDirectory(const StagingDirectory&) = delete;
    auto operator=(const StagingDirectory&) -> StagingDirectory& = delete;
    ~StagingDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
    Path path;
};

struct ProjectEntry
{
    Path path;
    Version version{};
    std::string name;
    bool ready{};
    Migration migration;
};

auto projectBackends(const Path& root) -> std::vector<db::BackendType>
{
    if (std::filesystem::is_symlink(root) || !std::filesystem::is_directory(root))
    {
        failure("Migration project must be a directory and cannot be a symlink");
    }
    const auto config = readJson(root / "project.json");
    fields(config, {"formatVersion", "backends"});
    if (!config.at("formatVersion").is_number_integer() || config.at("formatVersion") != 1)
        failure("Unsupported migration project format");
    const auto names = config.at("backends").get<std::vector<std::string>>();
    std::vector<db::BackendType> backends;
    for (const auto& name : names)
        backends.push_back(backendFromName(name));
    (void)backendNames(backends);
    std::ranges::sort(backends);
    return backends;
}

auto checkSnapshotBackends(const SchemaSnapshot& snapshot, std::span<const db::BackendType> backends) -> void
{
    if (snapshot.backends.size() != backends.size())
        failure("Snapshot backend set differs from project configuration");
    for (const auto backend : backends)
    {
        if (!snapshot.backends.contains(backend))
            failure("Snapshot backend set differs from project configuration");
    }
}

auto sqlFiles(const Path& entry, std::span<const db::BackendType> backends) -> std::map<std::string, std::string>
{
    std::map<std::string, std::string> files;
    files.emplace("migration.json", detail::sha256(readFile(entry / "migration.json")));
    files.emplace("schema.json", detail::sha256(readFile(entry / "schema.json")));
    for (const auto backend : backends)
    {
        const auto name = std::string{detail::backendName(backend)};
        const auto dialect = entry / name;
        if (!std::filesystem::is_directory(dialect) || std::filesystem::is_symlink(dialect))
        {
            failure("Missing or invalid project backend directory");
        }
        for (const auto& item : std::filesystem::directory_iterator(dialect))
        {
            if (item.path().filename() != "up.sql" && item.path().filename() != "down.sql")
            {
                failure("Unexpected file in backend directory");
            }
            regularFile(item.path());
        }
        files.emplace(name + "/up.sql", detail::sha256(detail::normalizeSql(readFile(dialect / "up.sql"))));
        if (std::filesystem::exists(dialect / "down.sql"))
        {
            files.emplace(name + "/down.sql", detail::sha256(detail::normalizeSql(readFile(dialect / "down.sql"))));
        }
    }
    return files;
}

auto loadEntries(const Path& root, std::span<const db::BackendType> backends) -> std::vector<ProjectEntry>
{
    if (projectBackends(root) != std::vector<db::BackendType>{backends.begin(), backends.end()})
    {
        failure("Migration project backend configuration has changed");
    }
    std::vector<ProjectEntry> result;
    for (const auto& item : std::filesystem::directory_iterator(root))
    {
        const auto name = item.path().filename().string();
        if (name == "project.json")
        {
            regularFile(item.path());
            continue;
        }
        if (name == ".orm-cxx-lock" && item.is_directory() && !item.is_symlink())
            continue;
        if (!item.is_directory() || item.is_symlink())
            failure("Expected a <version>_<name> migration directory");
        const auto separator = name.find('_');
        Version version{};
        if (separator == std::string::npos)
            failure("Invalid migration directory name");
        const auto parsed = std::from_chars(name.data(), name.data() + separator, version);
        if (parsed.ec != std::errc{} || parsed.ptr != name.data() + separator || version <= 0 ||
            !safeProjectName(std::string_view{name}.substr(separator + 1)))
            failure("Invalid migration directory name");
        ProjectEntry entry;
        entry.path = item.path();
        entry.version = version;
        entry.name = name.substr(separator + 1);
        entry.ready = std::filesystem::exists(entry.path / "ready.json");
        result.push_back(std::move(entry));
    }
    std::ranges::sort(result, {}, &ProjectEntry::version);
    auto before = emptySnapshot(backends);
    Version parent{};
    bool draft{};
    for (auto& entry : result)
    {
        if (entry.version <= parent || draft)
            failure("Migration versions must be unique and drafts must be last");
        for (const auto& item : std::filesystem::directory_iterator(entry.path))
        {
            const auto name = item.path().filename().string();
            const auto isBackend =
                std::ranges::any_of(backends, [&](auto backend) { return detail::backendName(backend) == name; });
            if (name == "migration.json" || name == "schema.json" || name == "ready.json")
                regularFile(item.path());
            else if (!isBackend)
                failure("Unexpected file in migration directory");
        }
        const auto metadata = readJson(entry.path / "migration.json");
        fields(metadata, {"formatVersion", "version", "name", "kind", "parentVersion", "parentHash", "scripts"});
        if (!metadata.at("formatVersion").is_number_integer() || metadata.at("formatVersion") != 1 ||
            !metadata.at("version").is_number_integer() || !metadata.at("parentVersion").is_number_integer() ||
            metadata.at("version").get<Version>() != entry.version ||
            metadata.at("name").get<std::string>() != entry.name ||
            (metadata.at("kind") != "generated" && metadata.at("kind") != "manual") ||
            metadata.at("parentVersion").get<Version>() != parent ||
            metadata.at("parentHash").get<std::string>() != detail::sha256(detail::snapshotJson(before)))
        {
            failure("Migration parent chain or directory identity is invalid");
        }
        auto after = detail::snapshotFromJson(readFile(entry.path / "schema.json"));
        checkSnapshotBackends(after, backends);
        Migration migration;
        migration.version = entry.version;
        migration.name = entry.name;
        migration.before = before;
        migration.after = after;
        const auto& scriptsMetadata = metadata.at("scripts");
        if (!scriptsMetadata.is_object() || scriptsMetadata.size() != backends.size())
            failure("Invalid migration backend metadata");
        for (const auto backend : backends)
        {
            const auto name = std::string{detail::backendName(backend)};
            const auto& value = scriptsMetadata.at(name);
            fields(value, {"beforeHash", "afterHash", "upContext", "downContext"});
            SqlMigration scripts;
            scripts.up = detail::normalizeSql(readFile(entry.path / name / "up.sql"));
            if (std::filesystem::exists(entry.path / name / "down.sql"))
            {
                scripts.down = detail::normalizeSql(readFile(entry.path / name / "down.sql"));
            }
            scripts.beforeHash = value.at("beforeHash").get<std::string>();
            scripts.afterHash = value.at("afterHash").get<std::string>();
            scripts.upContext = detail::contextFromJson(value.at("upContext").dump());
            scripts.downContext = detail::contextFromJson(value.at("downContext").dump());
            if (scripts.beforeHash != detail::snapshotHash(before, backend) ||
                scripts.afterHash != detail::snapshotHash(after, backend))
            {
                failure("Migration schema hashes do not match the snapshot chain");
            }
            migration.scripts.emplace(backend, std::move(scripts));
        }
        const auto actualFiles = sqlFiles(entry.path, backends);
        if (entry.ready)
        {
            const Catalog validation{migration};
            migration = validation.migrations().front();
            const auto ready = readJson(entry.path / "ready.json");
            fields(ready, {"formatVersion", "state", "files", "checksums"});
            if (!ready.at("formatVersion").is_number_integer() || ready.at("formatVersion") != 1 ||
                ready.at("state") != "ready" ||
                ready.at("files").get<std::map<std::string, std::string>>() != actualFiles)
            {
                failure("Ready migration files have been modified");
            }
            const auto& checksums = ready.at("checksums");
            if (!checksums.is_object() || checksums.size() != backends.size())
                failure("Invalid ready migration checksum set");
            for (const auto& [backend, scripts] : migration.scripts)
            {
                if (checksums.at(std::string{detail::backendName(backend)}).get<std::string>() !=
                    detail::checksum(scripts))
                {
                    failure("Ready migration execution metadata has been modified");
                }
            }
        }
        else
            draft = true;
        entry.migration = std::move(migration);
        before = std::move(after);
        parent = entry.version;
    }
    return result;
}

auto publishDraft(const Path& root, std::span<const db::BackendType> backends, const Migration& migration,
                  Version parent, bool manual) -> void
{
    Json scripts = Json::object();
    StagingDirectory staging{root};
    writeFile(staging.path / "schema.json", detail::snapshotJson(migration.after));
    for (const auto backend : backends)
    {
        const auto name = std::string{detail::backendName(backend)};
        const auto& script = migration.scripts.at(backend);
        scripts[name] = {{"beforeHash", detail::snapshotHash(migration.before, backend)},
                         {"afterHash", detail::snapshotHash(migration.after, backend)},
                         {"upContext", Json::parse(detail::contextJson(script.upContext))},
                         {"downContext", Json::parse(detail::contextJson(script.downContext))}};
        std::filesystem::create_directory(staging.path / name);
        writeFile(staging.path / name / "up.sql", detail::normalizeSql(script.up));
        if (script.down)
            writeFile(staging.path / name / "down.sql", detail::normalizeSql(*script.down));
    }
    writeFile(staging.path / "migration.json",
              Json{{"formatVersion", 1},
                   {"version", migration.version},
                   {"name", migration.name},
                   {"kind", manual ? "manual" : "generated"},
                   {"parentVersion", parent},
                   {"parentHash", detail::sha256(detail::snapshotJson(migration.before))},
                   {"scripts", scripts}}
                      .dump(2) +
                  '\n');
    std::filesystem::rename(staging.path, root / (std::to_string(migration.version) + '_' + migration.name));
}

template <typename Function>
auto projectOperation(Function&& function)
{
    try
    {
        return function();
    }
    catch (const Json::exception&)
    {
        failure("Invalid migration project metadata");
    }
    catch (const std::filesystem::filesystem_error&)
    {
        failure("Cannot access migration project files");
    }
    catch (const std::ios_base::failure&)
    {
        failure("Cannot write migration project files");
    }
    throw MigrationError{ErrorCode::InvalidCatalog, "Migration project operation failed"};
}
} // namespace

MigrationProject::MigrationProject(Path directory) : directory_{std::move(directory)}
{
    backends_ = projectOperation([&] { return projectBackends(directory_); });
    (void)projectOperation([&] { return loadEntries(directory_, backends_); });
}

auto MigrationProject::init(const Path& directory, std::span<const db::BackendType> backends) -> MigrationProject
{
    return projectOperation(
        [&]
        {
            const auto names = backendNames(backends);
            if (std::filesystem::is_symlink(directory))
                failure("Migration project directory cannot be a symlink");
            std::filesystem::create_directories(directory);
            ProjectLock lock{directory};
            for (const auto& item : std::filesystem::directory_iterator(directory))
            {
                if (item.path().filename() != ".orm-cxx-lock")
                    failure("Migration project initialization requires an empty directory");
            }
            const auto temporary = directory / ".project.json.tmp";
            try
            {
                writeFile(temporary, Json{{"formatVersion", 1}, {"backends", names}}.dump(2) + '\n');
                std::filesystem::rename(temporary, directory / "project.json");
            }
            catch (...)
            {
                std::error_code error;
                std::filesystem::remove(temporary, error);
                throw;
            }
            return MigrationProject{directory};
        });
}

auto MigrationProject::directory() const noexcept -> const Path&
{
    return directory_;
}
auto MigrationProject::backends() const noexcept -> std::span<const db::BackendType>
{
    return backends_;
}

auto MigrationProject::latestSnapshot() const -> SchemaSnapshot
{
    return projectOperation(
        [&]
        {
            const auto entries = loadEntries(directory_, backends_);
            if (!entries.empty() && !entries.back().ready)
                failure("Finalize the draft before using it as a source snapshot");
            return entries.empty() ? emptySnapshot(backends_) : entries.back().migration.after;
        });
}

auto MigrationProject::draftVersion() const -> std::optional<Version>
{
    return projectOperation(
        [&]() -> std::optional<Version>
        {
            const auto entries = loadEntries(directory_, backends_);
            return !entries.empty() && !entries.back().ready ? std::optional<Version>{entries.back().version} :
                                                               std::nullopt;
        });
}

auto MigrationProject::catalog() const -> Catalog
{
    return projectOperation(
        [&]
        {
            const auto entries = loadEntries(directory_, backends_);
            std::vector<Migration> migrations;
            for (const auto& entry : entries)
            {
                if (!entry.ready)
                    failure("A draft migration cannot be executed; finalize it first");
                migrations.push_back(entry.migration);
            }
            return Catalog{std::move(migrations)};
        });
}

auto MigrationProject::compare(model::SchemaView models, const GenerationDecisions& decisions) const -> SchemaDiff
{
    return migrations::compare(latestSnapshot(), capture(models, backends_), decisions);
}

auto MigrationProject::make(std::string name, model::SchemaView models, const GenerationDecisions& decisions) -> Version
{
    return projectOperation(
        [&]
        {
            if (!safeProjectName(name))
                failure("Migration name must contain only ASCII letters, digits, underscores or hyphens");
            ProjectLock lock{directory_};
            const auto entries = loadEntries(directory_, backends_);
            if (!entries.empty() && !entries.back().ready)
                failure("Finalize the current draft before creating another migration");
            const auto parent = entries.empty() ? Version{} : entries.back().version;
            if (parent == std::numeric_limits<Version>::max())
                failure("Migration version range is exhausted");
            const auto before = entries.empty() ? emptySnapshot(backends_) : entries.back().migration.after;
            const auto diff = migrations::compare(before, capture(models, backends_), decisions);
            if (diff.changes.empty())
                failure("Models have no schema changes; use make --manual for a data migration");
            auto generated = generate(parent + 1, std::move(name), diff, decisions);
            publishDraft(directory_, backends_, generated.migration, parent, false);
            return parent + 1;
        });
}

auto MigrationProject::makeManual(std::string name, model::SchemaView models) -> Version
{
    return projectOperation(
        [&]
        {
            if (!safeProjectName(name))
                failure("Invalid migration name");
            ProjectLock lock{directory_};
            const auto entries = loadEntries(directory_, backends_);
            if (!entries.empty() && !entries.back().ready)
                failure("Finalize the current draft before creating another migration");
            const auto parent = entries.empty() ? Version{} : entries.back().version;
            if (parent == std::numeric_limits<Version>::max())
                failure("Migration version range is exhausted");
            Migration migration;
            migration.version = parent + 1;
            migration.name = std::move(name);
            migration.before = entries.empty() ? emptySnapshot(backends_) : entries.back().migration.after;
            migration.after = capture(models, backends_);
            for (const auto backend : backends_)
            {
                SqlMigration scripts;
                scripts.up = "-- Write and review the migration SQL before finalizing.\n";
                migration.scripts.emplace(backend, std::move(scripts));
            }
            publishDraft(directory_, backends_, migration, parent, true);
            return parent + 1;
        });
}

auto MigrationProject::finalize(Version version, model::SchemaView models, bool acceptSchema) -> void
{
    projectOperation(
        [&]
        {
            if (!acceptSchema)
                failure("Finalization requires explicit acceptance of the declared schema");
            ProjectLock lock{directory_};
            const auto entries = loadEntries(directory_, backends_);
            if (entries.empty() || entries.back().version != version || entries.back().ready)
            {
                failure("Only the current draft migration can be finalized");
            }
            const auto& entry = entries.back();
            if (entry.migration.after != capture(models, backends_))
            {
                failure("Current models differ from the draft target snapshot");
            }
            const Catalog validation{entry.migration};
            const auto validatedMigrations = validation.migrations();
            Json checksums = Json::object();
            for (const auto& [backend, scripts] : validatedMigrations.front().scripts)
            {
                detail::validateScript(scripts.up, backend, version);
                if (scripts.down)
                    detail::validateScript(*scripts.down, backend, version);
                checksums[std::string{detail::backendName(backend)}] = detail::checksum(scripts);
            }
            const auto temporary = entry.path / ".ready.json.tmp";
            try
            {
                writeFile(temporary, Json{{"formatVersion", 1},
                                          {"state", "ready"},
                                          {"files", sqlFiles(entry.path, backends_)},
                                          {"checksums", checksums}}
                                             .dump(2) +
                                         '\n');
                std::filesystem::rename(temporary, entry.path / "ready.json");
            }
            catch (...)
            {
                std::error_code error;
                std::filesystem::remove(temporary, error);
                throw;
            }
        });
}
} // namespace orm::migrations
