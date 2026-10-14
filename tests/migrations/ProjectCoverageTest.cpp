module;

#include <filesystem>
#include <fstream>
#include <streambuf>

#include "externals/nlohmann_json/nlohmann/json.hpp"
#include "tests/UnitTestPrelude.hpp"

module orm;

import :internal;
import :migrations_internal;

namespace migration_project_coverage_models
{
struct GuardUser
{
    int id;
    std::string name;
    inline static constexpr orm::reflection::FixedString table_name{"guard_users"};
};
struct GuardRequiredUser
{
    int id;
    std::string name;
    int age;
    inline static constexpr orm::reflection::FixedString table_name{"guard_users"};
};
struct CodecParent
{
    int id;
    std::string name;
    inline static constexpr orm::reflection::FixedString table_name{"codec_parents"};
    inline static constexpr auto auto_increment_columns = orm::autoIncrement<&CodecParent::id>();
    inline static constexpr auto string_lengths = orm::stringLengths(orm::stringLength<&CodecParent::name, 39>());
};
struct CodecOwner
{
    int id;
    std::optional<CodecParent> parent;
    inline static constexpr orm::reflection::FixedString table_name{"codec_owners"};
};
using GuardSchema = orm::Schema<GuardUser>;
using RequiredGuardSchema = orm::Schema<GuardRequiredUser>;
using CodecSchema = orm::Schema<CodecParent, CodecOwner>;
} // namespace migration_project_coverage_models

namespace
{
using namespace orm::migrations;
using namespace migration_project_coverage_models;
using Json = nlohmann::json;
using Backend = orm::db::BackendType;
constexpr std::array sqliteOnly{Backend::Sqlite};

class GuardDirectory final
{
public:
    GuardDirectory()
        : root{std::filesystem::temp_directory_path() /
               ("orm_project_guards_" +
                std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()))}
    {
        std::filesystem::create_directory(root);
    }
    ~GuardDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
    auto projectPath() const -> std::filesystem::path
    {
        return root / "project";
    }
    auto write(const std::filesystem::path& name, std::string_view text) const -> void
    {
        const auto path = root / name;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output{path, std::ios::binary | std::ios::trunc};
        output.exceptions(std::ios::failbit | std::ios::badbit);
        output << text;
    }
    auto read(const std::filesystem::path& name) const -> std::string
    {
        std::ifstream input{root / name, std::ios::binary};
        input.exceptions(std::ios::failbit | std::ios::badbit);
        return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    }
    auto metadata(const Json& value) const -> void
    {
        write("project/1_initial/migration.json", value.dump(2));
    }
    auto initial() -> MigrationProject
    {
        auto project = MigrationProject::init(projectPath(), sqliteOnly);
        EXPECT_EQ(project.make("initial", GuardSchema::view), 1);
        return project;
    }
    auto call(std::initializer_list<std::string_view> arguments,
              orm::model::SchemaView models = GuardSchema::view) const -> std::tuple<int, std::string, std::string>
    {
        std::ostringstream output, errors;
        const auto code =
            runCommandLine(std::span{arguments.begin(), arguments.size()}, models, projectPath(), output, errors);
        return {code, output.str(), errors.str()};
    }
    std::filesystem::path root;
};

class RejectingOutput final : public std::streambuf
{
    auto overflow(int_type /*character*/) -> int_type override
    {
        throw std::runtime_error{"Output is unavailable"};
    }
};

auto codecSnapshot() -> SchemaSnapshot
{
    auto snapshot = capture(CodecSchema::view);
    for (auto& [backend, schema] : snapshot.backends)
    {
        (void)backend;
        auto parent = std::ranges::find(schema.tables, "codec_parents", &PhysicalTable::name);
        parent->indexes.push_back({"codec_name_unique", {"name"}, true});
        auto owner = std::ranges::find(schema.tables, "codec_owners", &PhysicalTable::name);
        owner->foreignKeys.front().onDelete = "CASCADE";
        owner->foreignKeys.front().onUpdate = "CASCADE";
    }
    orm::migrations::detail::canonicalize(snapshot);
    return snapshot;
}

auto declaredMigration() -> Migration
{
    Migration migration;
    migration.version = 7;
    migration.name = "declared";
    migration.before = capture(GuardSchema::view, sqliteOnly);
    migration.after = migration.before;
    migration.scripts.emplace(Backend::Sqlite, SqlMigration{.up = "SELECT 1;"});
    return migration;
}
} // namespace

TEST(MigrationProjectCoverageTest, SnapshotCodecPreservesForeignKeysIndexesLengthsAndIdentity)
{
    const auto expected = codecSnapshot();
    const auto restored = orm::migrations::detail::snapshotFromJson(orm::migrations::detail::snapshotJson(expected));
    EXPECT_EQ(restored, expected);
    for (const auto& [backend, schema] : restored.backends)
    {
        const auto owner = std::ranges::find(schema.tables, "codec_owners", &PhysicalTable::name);
        ASSERT_EQ(owner->foreignKeys.size(), 1);
        EXPECT_EQ(owner->foreignKeys[0].onDelete, "CASCADE");
        EXPECT_EQ(owner->foreignKeys[0].onUpdate, "CASCADE");
        EXPECT_EQ(orm::migrations::detail::snapshotHash(expected, backend),
                  orm::migrations::detail::snapshotHash(restored, backend));
    }
}

TEST(MigrationProjectCoverageTest, CodecRejectsMalformedCollectionShapesAndMissingFields)
{
    const auto encoded = Json::parse(orm::migrations::detail::snapshotJson(codecSnapshot()));
    for (const auto field : {"columns", "foreignKeys", "indexes"})
    {
        auto invalid = encoded;
        invalid["backends"]["sqlite"]["tables"][0][field] = Json::object();
        EXPECT_THROW((void)orm::migrations::detail::snapshotFromJson(invalid.dump()), MigrationError) << field;
    }
    auto invalid = encoded;
    invalid["backends"]["sqlite"]["tables"] = Json::object();
    EXPECT_THROW((void)orm::migrations::detail::snapshotFromJson(invalid.dump()), MigrationError);
    invalid = encoded;
    invalid.erase("formatVersion");
    invalid["futureVersion"] = 1;
    EXPECT_THROW((void)orm::migrations::detail::snapshotFromJson(invalid.dump()), MigrationError);
}

TEST(MigrationProjectCoverageTest, RebuildCodecRequiresActualSqliteSchemaAndTableDeclarations)
{
    const auto snapshots = codecSnapshot();
    MigrationExecutionContext context;
    context.mode = MigrationExecutionMode::SqliteRebuild;
    context.tables = {"codec_parents"};
    context.source = snapshots.backends.at(Backend::Sqlite);
    context.target = context.source;
    context.rebuildTables = {{"codec_parents", "codec_parents", "__orm_rebuild_1111111111111111", "id", "id"}};
    EXPECT_EQ(orm::migrations::detail::contextFromJson(orm::migrations::detail::contextJson(context)), context);
    auto encoded = Json::parse(orm::migrations::detail::contextJson(context));
    encoded["rebuildTables"] = Json::object();
    EXPECT_THROW((void)orm::migrations::detail::contextFromJson(encoded.dump()), MigrationError);
    context.tables.clear();
    EXPECT_THROW((void)orm::migrations::detail::contextFromJson(orm::migrations::detail::contextJson(context)),
                 MigrationError);
    context.tables = {"codec_parents"};
    context.rebuildTables.clear();
    context.source = snapshots.backends.at(Backend::Postgres);
    context.target = context.source;
    EXPECT_THROW((void)orm::migrations::detail::contextFromJson(orm::migrations::detail::contextJson(context)),
                 MigrationError);
}

TEST(MigrationProjectCoverageTest, TypedExpressionsRejectMissingObjectsAndUnknownTransformationFields)
{
    for (const auto text : {R"({"backfills":[{}]})", R"({"backfills":[1]})",
                            R"({"backfills":[{"table":"t","column":"x","expressions":{}}]})",
                            R"({"backfills":[{"table":"t","column":"x","expressions":[]}]})",
                            R"({"backfills":[{"table":"t","column":"x","future":1}]})"})
    {
        EXPECT_THROW((void)orm::migrations::detail::decisionsFromJson(text), MigrationError) << text;
    }
    const auto decisions = orm::migrations::detail::decisionsFromJson(
        R"({"downBackfills":[{"table":"t","column":"x","constant":18446744073709551615}],"downConversions":[{"table":"t","column":"y","expressions":{"sqlite":"x+1"}}]})");
    EXPECT_EQ(std::get<std::uint64_t>(*decisions.downBackfills.front().constant),
              (std::numeric_limits<std::uint64_t>::max)());
    EXPECT_EQ(decisions.downConversions.front().expressions.at(Backend::Sqlite), "x+1");
}

TEST(MigrationProjectCoverageTest, CatalogRejectsSnapshotBackendMismatchAndTamperedHashes)
{
    auto migration = declaredMigration();
    EXPECT_NO_THROW(Catalog{migration});
    migration.before = capture(GuardSchema::view);
    migration.after = migration.before;
    EXPECT_THROW(Catalog{migration}, MigrationError);
    migration = declaredMigration();
    constexpr std::array postgresOnly{Backend::Postgres};
    migration.before = capture(GuardSchema::view, postgresOnly);
    migration.after = migration.before;
    EXPECT_THROW(Catalog{migration}, MigrationError);
    for (const auto before : {false, true})
    {
        migration = declaredMigration();
        auto& scripts = migration.scripts.at(Backend::Sqlite);
        (before ? scripts.beforeHash : scripts.afterHash) = std::string(64, '0');
        EXPECT_THROW(Catalog{migration}, MigrationError);
    }
}

TEST(MigrationProjectCoverageTest, CatalogRejectsTableListsWithoutSchemasAndWrongRebuildBackend)
{
    auto migration = declaredMigration();
    auto& scripts = migration.scripts.at(Backend::Sqlite);
    scripts.upContext.tables = {"guard_users"};
    EXPECT_THROW(Catalog{migration}, MigrationError);
    scripts.upContext.mode = MigrationExecutionMode::SqliteRebuild;
    EXPECT_THROW(Catalog{migration}, MigrationError);
    scripts.upContext.source = migration.before.backends.at(Backend::Sqlite);
    scripts.upContext.target = scripts.upContext.source;
    scripts.upContext.tables.clear();
    EXPECT_THROW(Catalog{migration}, MigrationError);
}

TEST(MigrationProjectCoverageTest, ProjectConfigurationRequiresSupportedBackendVersionAndUniqueFields)
{
    for (const auto text :
         {"{", "[]", "{}", R"({"formatVersion":1,"backends":["sqlite"],"futureVersion":1})",
          R"({"formatVersion":1,"formatVersion":1,"backends":["sqlite"]})",
          R"({"formatVersion":2,"backends":["sqlite"]})", R"({"formatVersion":1,"backends":["oracle"]})",
          R"({"formatVersion":1,"backends":42})", R"({"futureVersion":1,"backends":["sqlite"]})"})
    {
        GuardDirectory directory;
        directory.write("project/project.json", text);
        EXPECT_THROW((void)MigrationProject{directory.projectPath()}, MigrationError) << text;
    }
    GuardDirectory directory;
    directory.write("occupied", "keep");
    EXPECT_THROW((void)MigrationProject::init(directory.root / "occupied", sqliteOnly), MigrationError);
    EXPECT_EQ(directory.read("occupied"), "keep");
}

TEST(MigrationProjectCoverageTest, DirectoryIdentityParentsAndSchemaHashesMustRemainConsistent)
{
    for (const auto field : {"kind", "parentHash", "version"})
    {
        GuardDirectory directory;
        const auto project = directory.initial();
        auto metadata = Json::parse(directory.read("project/1_initial/migration.json"));
        if (std::string_view{field} == "version")
            metadata[field] = 2;
        else
            metadata[field] = "invalid";
        directory.metadata(metadata);
        EXPECT_THROW((void)MigrationProject{directory.projectPath()}, MigrationError) << field;
        EXPECT_THROW((void)project.catalog(), MigrationError) << field;
    }
    for (const auto field : {"beforeHash", "afterHash"})
    {
        GuardDirectory directory;
        const auto project = directory.initial();
        auto metadata = Json::parse(directory.read("project/1_initial/migration.json"));
        metadata["scripts"]["sqlite"][field] = std::string(64, '0');
        directory.metadata(metadata);
        EXPECT_THROW((void)project.draftVersion(), MigrationError) << field;
    }
    GuardDirectory directory;
    const auto project = directory.initial();
    auto metadata = Json::parse(directory.read("project/1_initial/migration.json"));
    metadata["scripts"] = Json::object();
    directory.metadata(metadata);
    EXPECT_THROW((void)project.draftVersion(), MigrationError);
}

TEST(MigrationProjectCoverageTest, SnapshotBackendSetsAndReadyChecksumSetsCannotBeChanged)
{
    for (const auto differentBackend : {false, true})
    {
        GuardDirectory directory;
        const auto project = directory.initial();
        constexpr std::array postgresOnly{Backend::Postgres};
        const auto snapshot = differentBackend ? capture(GuardSchema::view, postgresOnly) : capture(GuardSchema::view);
        directory.write("project/1_initial/schema.json", orm::migrations::detail::snapshotJson(snapshot));
        EXPECT_THROW((void)project.draftVersion(), MigrationError);
    }
    GuardDirectory directory;
    auto project = directory.initial();
    project.finalize(1, GuardSchema::view, true);
    auto ready = Json::parse(directory.read("project/1_initial/ready.json"));
    ready["checksums"] = Json::object();
    directory.write("project/1_initial/ready.json", ready.dump());
    EXPECT_THROW((void)project.latestSnapshot(), MigrationError);
}

TEST(MigrationProjectCoverageTest, UnexpectedBackendFilesAndMissingBackendDirectoriesAreRejected)
{
    GuardDirectory directory;
    const auto project = directory.initial();
    directory.write("project/1_initial/sqlite/notes.txt", "keep");
    EXPECT_THROW((void)project.draftVersion(), MigrationError);
    std::filesystem::remove(directory.projectPath() / "1_initial/sqlite/notes.txt");
    std::filesystem::rename(directory.projectPath() / "1_initial/sqlite", directory.root / "saved_backend");
    EXPECT_THROW((void)project.draftVersion(), MigrationError);
}

TEST(MigrationProjectCoverageTest, SymlinkedProjectsAndBackendDirectoriesCannotBypassFileValidation)
{
    GuardDirectory directory;
    const auto project = directory.initial();
    const auto saved = directory.root / "saved_backend";
    const auto backend = directory.projectPath() / "1_initial/sqlite";
    std::filesystem::rename(backend, saved);
    std::error_code error;
    std::filesystem::create_directory_symlink(saved, backend, error);
    if (error)
        GTEST_SKIP() << "Directory symlinks are unavailable: " << error.message();
    EXPECT_THROW((void)project.draftVersion(), MigrationError);
    const auto alias = directory.root / "alias";
    std::filesystem::create_directory_symlink(directory.projectPath(), alias);
    EXPECT_THROW((void)MigrationProject{alias}, MigrationError);
    EXPECT_THROW((void)MigrationProject::init(alias, sqliteOnly), MigrationError);
}

TEST(MigrationProjectCoverageTest, ManualDraftRequiresPortableNameAndFinalizationBeforeAnotherDraft)
{
    GuardDirectory directory;
    auto project = MigrationProject::init(directory.projectPath(), sqliteOnly);
    EXPECT_EQ(project.directory(), directory.projectPath());
    EXPECT_EQ(std::vector<Backend>(project.backends().begin(), project.backends().end()),
              (std::vector<Backend>{Backend::Sqlite}));
    EXPECT_THROW((void)project.makeManual("bad name", GuardSchema::view), MigrationError);
    EXPECT_EQ(project.makeManual("data", GuardSchema::view), 1);
    EXPECT_THROW((void)project.makeManual("other", GuardSchema::view), MigrationError);
    EXPECT_FALSE(std::filesystem::exists(directory.projectPath() / ".orm-cxx-lock"));
}

TEST(MigrationProjectCoverageTest, OfflineCliValidatesBackendListsAndOptionGrammar)
{
    GuardDirectory directory;
    for (const auto& args : {std::vector<std::string_view>{"init", "--backends", "sqlite,mysql,"},
                             {"init", "--backends", "oracle"},
                             {"init", "--backends", ""},
                             {"init", "wrong"},
                             {"make"},
                             {"make", "--manual"},
                             {"compare", "--unknown"},
                             {"compare", "--check", "--check"}})
    {
        std::ostringstream output, errors;
        EXPECT_EQ(runCommandLine(args, GuardSchema::view, directory.projectPath(), output, errors), 2);
        EXPECT_FALSE(errors.str().empty());
    }
    EXPECT_EQ(std::get<0>(directory.call({"init"})), 0);
    EXPECT_EQ(MigrationProject{directory.projectPath()}.backends().size(), generationBackends.size());
}

TEST(MigrationProjectCoverageTest, OfflineCliReportsUnresolvedChangesAndConsumesDecisionFiles)
{
    GuardDirectory directory;
    auto project = directory.initial();
    project.finalize(1, GuardSchema::view, true);
    const auto unresolved = directory.call({"compare"}, RequiredGuardSchema::view);
    EXPECT_EQ(std::get<0>(unresolved), 0);
    EXPECT_NE(std::get<1>(unresolved).find("[decision required]"), std::string::npos);
    EXPECT_TRUE(std::get<2>(unresolved).empty());
    EXPECT_EQ(std::get<0>(directory.call({"compare", "--check"}, RequiredGuardSchema::view)), 1);
    const auto failedGeneration = directory.call({"make", "required"}, RequiredGuardSchema::view);
    EXPECT_EQ(std::get<0>(failedGeneration), 1);
    EXPECT_FALSE(std::get<2>(failedGeneration).empty());
    const auto missing = (directory.root / "missing.json").string();
    EXPECT_EQ(std::get<0>(directory.call({"compare", "--decisions", missing})), 1);
    directory.write("invalid-decisions.json", R"({"tableRenames":[{"from":"missing","to":"guard_users"}]})");
    const auto invalidFile = (directory.root / "invalid-decisions.json").string();
    const auto invalid = directory.call({"compare", "--decisions", invalidFile});
    EXPECT_EQ(std::get<0>(invalid), 1);
    EXPECT_NE(std::get<2>(invalid).find("invalid-rename"), std::string::npos);
    directory.write("decisions.json", R"({"backfills":[{"table":"guard_users","column":"age","constant":18}]})");
    const auto file = (directory.root / "decisions.json").string();
    EXPECT_EQ(std::get<0>(directory.call({"compare", "--decisions", file}, RequiredGuardSchema::view)), 0);
    EXPECT_EQ(std::get<0>(directory.call({"make", "required", "--decisions", file}, RequiredGuardSchema::view)), 0);
    EXPECT_EQ(project.draftVersion(), 2);
    EXPECT_EQ(std::get<0>(directory.call({"finalize", "2", "--accept-schema"}, RequiredGuardSchema::view)), 0);
}

TEST(MigrationProjectCoverageTest, OfflineCliExecutorDispatchRejectsOptionsAndInvalidProjects)
{
    GuardDirectory directory;
    auto project = directory.initial();
    project.finalize(1, GuardSchema::view, true);
    EXPECT_EQ(std::get<0>(directory.call({"status", "--unknown"})), 2);
    directory.write("project/project.json", "invalid");
    const auto failed = directory.call({"compare"});
    EXPECT_EQ(std::get<0>(failed), 1);
    EXPECT_FALSE(std::get<2>(failed).empty());
}

TEST(MigrationProjectCoverageTest, OfflineCliReportsOutputFailureAfterAtomicProjectInitialization)
{
    GuardDirectory directory;
    RejectingOutput buffer;
    std::ostream output{&buffer};
    output.exceptions(std::ios::failbit | std::ios::badbit);
    std::ostringstream errors;
    const std::array<std::string_view, 1> arguments{"init"};
    EXPECT_EQ(runCommandLine(arguments, GuardSchema::view, directory.projectPath(), output, errors), 1);
    EXPECT_FALSE(errors.str().empty());
    EXPECT_TRUE(std::filesystem::exists(directory.projectPath() / "project.json"));
    EXPECT_FALSE(std::filesystem::exists(directory.projectPath() / ".orm-cxx-lock"));
    EXPECT_NO_THROW((void)MigrationProject{directory.projectPath()});
}
