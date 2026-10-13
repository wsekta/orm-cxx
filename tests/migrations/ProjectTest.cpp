module;

#include <filesystem>
#include <fstream>

#include "externals/nlohmann_json/nlohmann/json.hpp"
#include "tests/UnitTestPrelude.hpp"

module orm;

import :internal;
import :migrations_internal;

namespace
{
using namespace orm::migrations;
using ProjectJson = nlohmann::json;
constexpr std::array sqliteOnly{orm::db::BackendType::Sqlite};

struct ProjectUser
{
    int id;
    std::string name;
    inline static constexpr orm::reflection::FixedString table_name{"project_users"};
};
struct ProjectUserWithEmail
{
    int id;
    std::string name;
    std::optional<std::string> email;
    inline static constexpr orm::reflection::FixedString table_name{"project_users"};
};
struct ProjectUserWithEmailAndAge
{
    int id;
    std::string name;
    std::optional<std::string> email;
    std::optional<int> age;
    inline static constexpr orm::reflection::FixedString table_name{"project_users"};
};
using ProjectSchema = orm::Schema<ProjectUser>;
using ChangedProjectSchema = orm::Schema<ProjectUserWithEmail>;
using ExtendedProjectSchema = orm::Schema<ProjectUserWithEmailAndAge>;

class ProjectDirectory final
{
public:
    ProjectDirectory()
        : path{std::filesystem::temp_directory_path() /
               ("orm_model_project_" +
                std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()))}
    {
        std::filesystem::create_directory(path);
    }
    ~ProjectDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
    auto write(const std::filesystem::path& file, std::string_view value) const -> void
    {
        std::ofstream stream{path / file, std::ios::binary | std::ios::trunc};
        stream << value;
    }
    auto read(const std::filesystem::path& file) const -> std::string
    {
        std::ifstream stream{path / file, std::ios::binary};
        return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
    }
    std::filesystem::path path;
};

auto makeReady(ProjectDirectory& directory) -> MigrationProject
{
    auto project = MigrationProject::init(directory.path, sqliteOnly);
    EXPECT_EQ(project.make("initial", ProjectSchema::view), 1);
    project.finalize(1, ProjectSchema::view, true);
    return project;
}

auto changedJson(std::string text, std::string_view from, std::string_view to) -> std::string
{
    const auto position = text.find(from);
    if (position == std::string::npos)
        throw std::logic_error{"Test metadata field was not found"};
    text.replace(position, from.size(), to);
    return text;
}

auto rehashReadyMetadata(const ProjectDirectory& directory, const std::filesystem::path& entry,
                         const ProjectJson& metadata, const SqlMigration* scripts = nullptr) -> void
{
    const auto text = metadata.dump(2) + '\n';
    directory.write(entry / "migration.json", text);
    auto ready = ProjectJson::parse(directory.read(entry / "ready.json"));
    ready["files"]["migration.json"] = orm::migrations::detail::sha256(text);
    if (scripts)
        ready["checksums"]["sqlite"] = orm::migrations::detail::checksum(*scripts);
    directory.write(entry / "ready.json", ready.dump(2) + '\n');
}
} // namespace

TEST(MigrationProjectTest, DraftFinalizationAndImmutableChain)
{
    ProjectDirectory directory;
    auto project = MigrationProject::init(directory.path, sqliteOnly);
    EXPECT_TRUE(project.latestSnapshot().backends.at(orm::db::BackendType::Sqlite).tables.empty());
    EXPECT_TRUE(project.catalog().migrations().empty());
    EXPECT_FALSE(project.draftVersion());
    EXPECT_EQ(project.make("initial", ProjectSchema::view), 1);
    EXPECT_EQ(project.draftVersion(), 1);
    EXPECT_THROW((void)project.latestSnapshot(), MigrationError);
    EXPECT_THROW((void)project.catalog(), MigrationError);
    EXPECT_THROW((void)project.make("another", ProjectSchema::view), MigrationError);
    EXPECT_THROW(project.finalize(1, ProjectSchema::view, false), MigrationError);
    EXPECT_THROW(project.finalize(1, ChangedProjectSchema::view, true), MigrationError);
    EXPECT_THROW(project.finalize(2, ProjectSchema::view, true), MigrationError);
    project.finalize(1, ProjectSchema::view, true);
    EXPECT_FALSE(project.draftVersion());
    EXPECT_EQ(project.latestSnapshot(), capture(ProjectSchema::view, sqliteOnly));
    EXPECT_TRUE(project.compare(ProjectSchema::view).changes.empty());
    EXPECT_FALSE(project.compare(ChangedProjectSchema::view).changes.empty());
    EXPECT_THROW(project.finalize(1, ProjectSchema::view, true), MigrationError);
    EXPECT_EQ(project.make("email", ChangedProjectSchema::view), 2);
    project.finalize(2, ChangedProjectSchema::view, true);
    const auto catalog = Catalog::fromDirectory(directory.path);
    ASSERT_EQ(catalog.migrations().size(), 2);
    EXPECT_EQ(catalog.migrations()[0].after, catalog.migrations()[1].before);
    EXPECT_FALSE(std::filesystem::exists(directory.path / ".orm-cxx-lock"));
    EXPECT_FALSE(std::filesystem::exists(directory.path / ".orm-cxx-staging"));
}

TEST(MigrationProjectTest, ReadyFilesAndParentSnapshotsAreProtected)
{
    for (const auto& file :
         {"1_initial/sqlite/up.sql", "1_initial/schema.json", "1_initial/migration.json", "1_initial/ready.json"})
    {
        ProjectDirectory directory;
        const auto project = makeReady(directory);
        const auto original = directory.read(file);
        directory.write(file, original + "modified");
        EXPECT_THROW((void)project.catalog(), MigrationError) << file;
        EXPECT_THROW((void)MigrationProject{directory.path}, MigrationError) << file;
    }
    ProjectDirectory directory;
    const auto project = makeReady(directory);
    directory.write("project.json", "{\"formatVersion\":1,\"backends\":[\"mysql\"]}");
    EXPECT_THROW((void)project.catalog(), MigrationError);
    EXPECT_THROW((void)MigrationProject{directory.path}, MigrationError);
}

TEST(MigrationProjectTest, RehashedReadyContextsMustMatchTheSchemaChainForEveryOfflineOperation)
{
    for (const auto down : {false, true})
    {
        for (const auto source : {false, true})
        {
            SCOPED_TRACE(std::string{down ? "down" : "up"} + (source ? " source" : " target"));
            ProjectDirectory directory;
            auto project = makeReady(directory);
            GenerationDecisions decisions;
            decisions.allowDataLossDown = {"sqlite/drop-column/project_users/email"};
            ASSERT_EQ(project.make("email", ChangedProjectSchema::view, decisions), 2);
            project.finalize(2, ChangedProjectSchema::view, true);
            auto migration = project.catalog().migrations().back();
            auto& scripts = migration.scripts.at(orm::db::BackendType::Sqlite);
            auto& context = down ? scripts.downContext : scripts.upContext;
            auto& schema = source ? context.source : context.target;
            ASSERT_TRUE(schema);
            auto& columns = schema->tables.front().columns;
            const auto name = std::ranges::find(columns, "name", &PhysicalColumn::name);
            ASSERT_NE(name, columns.end());
            name->nullable = !name->nullable;
            const auto field = down ? "downContext" : "upContext";
            auto metadata = ProjectJson::parse(directory.read("2_email/migration.json"));
            metadata["scripts"]["sqlite"][field] = ProjectJson::parse(orm::migrations::detail::contextJson(context));
            rehashReadyMetadata(directory, "2_email", metadata, &scripts);

            EXPECT_THROW(Catalog{migration}, MigrationError);
            EXPECT_THROW((void)MigrationProject{directory.path}, MigrationError);
            EXPECT_THROW((void)project.latestSnapshot(), MigrationError);
            EXPECT_THROW((void)project.compare(ChangedProjectSchema::view), MigrationError);
            EXPECT_THROW((void)project.make("age", ExtendedProjectSchema::view), MigrationError);
            EXPECT_FALSE(std::filesystem::exists(directory.path / "3_age"));
            EXPECT_FALSE(std::filesystem::exists(directory.path / ".orm-cxx-lock"));
        }
    }
}

TEST(MigrationProjectTest, ReadyExecutionChecksumIsValidatedIndependentlyOfFileHashes)
{
    ProjectDirectory directory;
    const auto project = makeReady(directory);
    auto ready = ProjectJson::parse(directory.read("1_initial/ready.json"));
    ready["checksums"]["sqlite"] = std::string(64, '0');
    directory.write("1_initial/ready.json", ready.dump(2) + '\n');
    EXPECT_THROW((void)MigrationProject{directory.path}, MigrationError);
    EXPECT_THROW((void)project.latestSnapshot(), MigrationError);
}

TEST(MigrationProjectTest, ExhaustedVersionsCannotCreateGeneratedOrManualDrafts)
{
    ProjectDirectory directory;
    auto project = makeReady(directory);
    const auto maximum = std::numeric_limits<Version>::max();
    const auto entry = std::to_string(maximum) + "_initial";
    auto metadata = ProjectJson::parse(directory.read("1_initial/migration.json"));
    metadata["version"] = maximum;
    rehashReadyMetadata(directory, "1_initial", metadata);
    std::filesystem::rename(directory.path / "1_initial", directory.path / entry);
    EXPECT_NO_THROW((void)MigrationProject{directory.path});
    EXPECT_EQ(project.catalog().migrations().front().version, maximum);
    EXPECT_THROW((void)project.make("email", ChangedProjectSchema::view), MigrationError);
    EXPECT_THROW((void)project.makeManual("data", ProjectSchema::view), MigrationError);
    EXPECT_FALSE(project.draftVersion());
    EXPECT_FALSE(std::filesystem::exists(directory.path / ".orm-cxx-lock"));
    EXPECT_FALSE(std::filesystem::exists(directory.path / ".orm-cxx-staging"));
}

TEST(MigrationProjectTest, WriterLockAndInvalidDecisionsLeaveNoPublishedMigration)
{
    ProjectDirectory directory;
    auto project = MigrationProject::init(directory.path, sqliteOnly);
    std::filesystem::create_directory(directory.path / ".orm-cxx-lock");
    EXPECT_THROW((void)project.make("initial", ProjectSchema::view), MigrationError);
    std::filesystem::remove(directory.path / ".orm-cxx-lock");
    EXPECT_THROW((void)project.make("bad name", ProjectSchema::view), MigrationError);
    EXPECT_FALSE(project.draftVersion());
    EXPECT_THROW((void)project.make("no_change", orm::model::SchemaView{}), MigrationError);
    EXPECT_EQ(project.make("initial", ProjectSchema::view), 1);
}

TEST(MigrationProjectTest, UnpublishedStagingAndTemporaryMarkersCannotBecomeReadyMigrations)
{
    ProjectDirectory directory;
    auto project = MigrationProject::init(directory.path, sqliteOnly);
    std::filesystem::create_directory(directory.path / ".orm-cxx-staging");
    directory.write(".orm-cxx-staging/up.sql", "SELECT 1;");
    EXPECT_THROW((void)project.make("initial", ProjectSchema::view), MigrationError);
    EXPECT_EQ(directory.read(".orm-cxx-staging/up.sql"), "SELECT 1;");
    EXPECT_FALSE(std::filesystem::exists(directory.path / ".orm-cxx-lock"));
    std::filesystem::remove(directory.path / ".orm-cxx-staging/up.sql");
    std::filesystem::remove(directory.path / ".orm-cxx-staging");
    ASSERT_EQ(project.make("initial", ProjectSchema::view), 1);
    directory.write("1_initial/.ready.json.tmp", "{\"state\":\"ready\"}");
    EXPECT_THROW((void)MigrationProject{directory.path}, MigrationError);
    EXPECT_THROW(project.finalize(1, ProjectSchema::view, true), MigrationError);
    EXPECT_FALSE(std::filesystem::exists(directory.path / "1_initial/ready.json"));
    EXPECT_FALSE(std::filesystem::exists(directory.path / ".orm-cxx-lock"));
    std::filesystem::remove(directory.path / "1_initial/.ready.json.tmp");
    directory.write("1_initial/sqlite/up.sql", "");
    EXPECT_EQ(MigrationProject{directory.path}.draftVersion(), 1);
    EXPECT_THROW(project.finalize(1, ProjectSchema::view, true), MigrationError);
    directory.write("1_initial/sqlite/up.sql", "SELECT 1;");
    project.finalize(1, ProjectSchema::view, true);
    EXPECT_FALSE(project.draftVersion());
    EXPECT_FALSE(std::filesystem::exists(directory.path / "1_initial/.ready.json.tmp"));
}

TEST(MigrationProjectTest, ManualSqlRequiresExplicitSchemaAcceptance)
{
    ProjectDirectory directory;
    auto project = MigrationProject::init(directory.path, sqliteOnly);
    EXPECT_EQ(project.makeManual("data", orm::model::SchemaView{}), 1);
    directory.write("1_data/sqlite/up.sql", "SELECT 1;\r\n");
    directory.write("1_data/sqlite/down.sql", "");
    EXPECT_THROW(project.finalize(1, orm::model::SchemaView{}, false), MigrationError);
    project.finalize(1, orm::model::SchemaView{}, true);
    const auto catalog = project.catalog();
    const auto& scripts = catalog.migrations()[0].scripts.at(orm::db::BackendType::Sqlite);
    EXPECT_EQ(scripts.up, "SELECT 1;\n");
    EXPECT_EQ(scripts.down, "");
    EXPECT_FALSE(scripts.beforeHash.empty());
    EXPECT_EQ(scripts.beforeHash, scripts.afterHash);
}

TEST(MigrationProjectTest, JsonRoundTripsAndSelectedBackendHashes)
{
    const auto snapshot = capture(ProjectSchema::view);
    EXPECT_EQ(orm::migrations::detail::snapshotFromJson(orm::migrations::detail::snapshotJson(snapshot)), snapshot);
    auto changed = snapshot;
    auto& mysqlColumns = changed.backends.at(orm::db::BackendType::Mysql).tables[0].columns;
    std::ranges::find(mysqlColumns, "name", &PhysicalColumn::name)->nullable = true;
    EXPECT_EQ(orm::migrations::detail::snapshotHash(snapshot, orm::db::BackendType::Sqlite),
              orm::migrations::detail::snapshotHash(changed, orm::db::BackendType::Sqlite));
    EXPECT_NE(orm::migrations::detail::snapshotHash(snapshot, orm::db::BackendType::Mysql),
              orm::migrations::detail::snapshotHash(changed, orm::db::BackendType::Mysql));
    MigrationExecutionContext context;
    context.mode = MigrationExecutionMode::SqliteRebuild;
    context.tables = {"project_users"};
    context.rebuildTables.push_back({.source = "project_users",
                                     .target = "project_users",
                                     .staging = "__orm_rebuild_0123456789abcdef",
                                     .sourceIdentity = "id",
                                     .targetIdentity = "id"});
    context.source = snapshot.backends.at(orm::db::BackendType::Sqlite);
    context.target = context.source;
    EXPECT_EQ(orm::migrations::detail::contextFromJson(orm::migrations::detail::contextJson(context)), context);
    SqlMigration scripts;
    scripts.up = "SELECT 1;";
    const auto original = orm::migrations::detail::checksum(scripts);
    scripts.upContext = context;
    EXPECT_NE(original, orm::migrations::detail::checksum(scripts));
    EXPECT_THROW((void)orm::migrations::detail::snapshotFromJson("{}"), MigrationError);
    EXPECT_THROW((void)orm::migrations::detail::contextFromJson("{invalid"), MigrationError);
}

TEST(MigrationProjectTest, TypedDecisionParsingRejectsAmbiguousAndUnknownFields)
{
    const auto decisions = orm::migrations::detail::decisionsFromJson(
        R"json({"tableRenames":[{"from":"users","to":"people"}],
            "columnRenames":[{"table":"people","from":"name","to":"label"}],
            "allowDataLossUp":["drop-column:people.email"],"allowDataLossDown":[],
            "backfills":[{"table":"people","column":"age","constant":18},
                        {"table":"people","column":"name","sourceColumn":"label"},
                        {"table":"people","column":"note","constant":null},
                        {"table":"people","column":"active","constant":true},
                        {"table":"people","column":"ratio","constant":1.5},
                        {"table":"people","column":"text","constant":"ready"}],
            "conversions":[{"table":"people","column":"value","expressions":{"sqlite":"CAST(value AS TEXT)"}}],
            "downBackfills":[],"downConversions":[]})json");
    ASSERT_EQ(decisions.tableRenames.size(), 1);
    ASSERT_EQ(decisions.backfills.size(), 6);
    EXPECT_TRUE(decisions.backfills[2].constant);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(*decisions.backfills[2].constant));
    EXPECT_EQ(decisions.conversions[0].expressions.at(orm::db::BackendType::Sqlite), "CAST(value AS TEXT)");
    const auto negative = orm::migrations::detail::decisionsFromJson(
        R"({"backfills":[{"table":"people","column":"age","constant":-18}]})");
    EXPECT_EQ(std::get<std::int64_t>(*negative.backfills[0].constant), -18);
    for (const auto text :
         {"{", "[]", "{\"unknown\":[]}", "{\"backfills\":{}}", R"({"backfills":[{"table":"t","column":"x"}]})",
          R"({"backfills":[{"table":"t","column":"x","constant":1,"sourceColumn":"x"}]})",
          R"({"backfills":[{"table":"t","column":"x","constant":[]}]})",
          R"({"conversions":[{"table":"t","column":"x","expressions":{"oracle":"1"}}]})"})
    {
        EXPECT_THROW((void)orm::migrations::detail::decisionsFromJson(text), MigrationError) << text;
    }
}

TEST(MigrationProjectTest, JsonMetadataRequiresStrictFieldsAndPhysicalValues)
{
    const auto snapshot = orm::migrations::detail::snapshotJson(capture(ProjectSchema::view, sqliteOnly));
    for (const auto& [from, to] : std::vector<std::pair<std::string, std::string>>{
             {"\"formatVersion\": 1", "\"formatVersion\": -1"},
             {"\"formatVersion\": 1", "\"formatVersion\": 1.5"},
             {"\"formatVersion\": 1", "\"formatVersion\": 1,\"formatVersion\": 1"},
             {"\"backend\": \"sqlite\"", "\"backend\": \"oracle\""},
             {"\"backend\": \"sqlite\"", "\"backend\": \"mysql\""},
             {"\"logicalType\": 5", "\"logicalType\": 999"},
             {"\"logicalType\": 5", "\"logicalType\": 5.5"},
             {"\"maxStringLength\": null", "\"maxStringLength\": -1"},
             {"\"nullable\": false", "\"nullable\": 1"},
             {"\"sqlType\": \"INTEGER\"", "\"sqlType\": \"; DROP TABLE project_users\""}})
    {
        EXPECT_THROW((void)orm::migrations::detail::snapshotFromJson(changedJson(snapshot, from, to)), MigrationError)
            << from << " -> " << to;
    }
    EXPECT_THROW((void)orm::migrations::detail::snapshotFromJson("{\"formatVersion\":1,\"backends\":[]}"),
                 MigrationError);
    EXPECT_THROW((void)orm::migrations::detail::snapshotFromJson("{\"formatVersion\":1,\"backends\":{},\"unknown\":1}"),
                 MigrationError);
    EXPECT_THROW((void)orm::migrations::detail::decisionsFromJson("{\"allowDataLossUp\":[],\"allowDataLossUp\":[]}"),
                 MigrationError);
    MigrationExecutionContext normal;
    const auto context = orm::migrations::detail::contextJson(normal);
    EXPECT_THROW((void)orm::migrations::detail::contextFromJson(
                     changedJson(context, "\"mode\": \"normal\"", "\"mode\": \"unknown\"")),
                 MigrationError);
    EXPECT_THROW((void)orm::migrations::detail::contextFromJson(
                     changedJson(context, "\"tables\": []", "\"tables\": [\"project_users\"]")),
                 MigrationError);
    normal.mode = MigrationExecutionMode::SqliteRebuild;
    normal.tables = {"project_users"};
    normal.source = capture(ProjectSchema::view, sqliteOnly).backends.at(orm::db::BackendType::Sqlite);
    normal.target = emptySnapshot(sqliteOnly).backends.at(orm::db::BackendType::Sqlite);
    EXPECT_EQ(orm::migrations::detail::contextFromJson(orm::migrations::detail::contextJson(normal)), normal);
}

TEST(MigrationProjectTest, CatalogChecksSnapshotExecutionContexts)
{
    Migration migration;
    migration.version = 1;
    migration.name = "context";
    migration.before = capture(ProjectSchema::view, sqliteOnly);
    migration.after = migration.before;
    SqlMigration scripts;
    scripts.up = "SELECT 1;";
    scripts.upContext.source = migration.before.backends.at(orm::db::BackendType::Sqlite);
    scripts.upContext.target = scripts.upContext.source;
    scripts.upContext.tables = {"project_users"};
    migration.scripts.emplace(orm::db::BackendType::Sqlite, scripts);
    EXPECT_NO_THROW(Catalog{migration});
    auto invalid = migration;
    invalid.scripts.begin()->second.upContext.tables.push_back("project_users");
    EXPECT_THROW(Catalog{invalid}, MigrationError);
    invalid = migration;
    invalid.scripts.begin()->second.upContext.tables = {"unknown"};
    EXPECT_THROW(Catalog{invalid}, MigrationError);
    invalid = migration;
    invalid.scripts.begin()->second.upContext.target.reset();
    EXPECT_THROW(Catalog{invalid}, MigrationError);
    invalid = migration;
    invalid.scripts.begin()->second.upContext.source->tables[0].columns[0].nullable = true;
    EXPECT_THROW(Catalog{invalid}, MigrationError);
    invalid = migration;
    auto& context = invalid.scripts.begin()->second.upContext;
    context.mode = MigrationExecutionMode::SqliteRebuild;
    context.rebuildTables.push_back({.source = "project_users",
                                     .target = "project_users",
                                     .staging = "__orm_rebuild_0123456789abcdef",
                                     .sourceIdentity = std::nullopt,
                                     .targetIdentity = std::nullopt});
    EXPECT_NO_THROW(Catalog{invalid});
    context.rebuildTables[0].sourceIdentity = "id";
    EXPECT_THROW(Catalog{invalid}, MigrationError);
    context.rebuildTables[0].sourceIdentity.reset();
    context.rebuildTables[0].staging = "project_users";
    EXPECT_THROW(Catalog{invalid}, MigrationError);
    context.rebuildTables[0].staging = "__orm_rebuild_invalidinvalidxx";
    EXPECT_THROW(Catalog{invalid}, MigrationError);
}

TEST(MigrationProjectTest, OfflineCliCreatesReviewsAndChecksModels)
{
    ProjectDirectory directory;
    const auto call = [&](std::initializer_list<std::string_view> arguments)
    {
        std::ostringstream output, errors;
        const auto code = runCommandLine(std::span{arguments.begin(), arguments.size()}, ProjectSchema::view,
                                         directory.path, output, errors);
        return std::tuple{code, output.str(), errors.str()};
    };
    EXPECT_EQ(std::get<0>(call({"init", "--backends", "sqlite"})), 0);
    EXPECT_EQ(std::get<0>(call({"compare", "--check"})), 1);
    EXPECT_EQ(std::get<0>(call({"make", "initial"})), 0);
    EXPECT_EQ(std::get<0>(call({"finalize", "1", "--accept-schema"})), 0);
    EXPECT_EQ(std::get<0>(call({"compare", "--check"})), 0);
    EXPECT_EQ(std::get<0>(call({"make", "data", "--manual"})), 0);
    EXPECT_EQ(std::get<0>(call({"finalize", "2"})), 2);
    EXPECT_EQ(std::get<0>(call({"finalize", "bad", "--accept-schema"})), 2);
    EXPECT_EQ(std::get<0>(call({"make", "x", "--manual", "--decisions", "x.json"})), 2);
    EXPECT_EQ(std::get<0>(call({"unknown"})), 2);
}

TEST(MigrationProjectTest, InitRejectsOldFormatAndUnexpectedEntries)
{
    ProjectDirectory directory;
    std::filesystem::create_directories(directory.path / "1_old/sqlite");
    directory.write("1_old/sqlite/up.sql", "SELECT 1;");
    EXPECT_THROW((void)Catalog::fromDirectory(directory.path), MigrationError);
    EXPECT_THROW((void)MigrationProject::init(directory.path), MigrationError);
    ProjectDirectory empty;
    const std::array<orm::db::BackendType, 0> none{};
    EXPECT_THROW((void)MigrationProject::init(empty.path, none), MigrationError);
    constexpr std::array duplicate{orm::db::BackendType::Sqlite, orm::db::BackendType::Sqlite};
    EXPECT_THROW((void)MigrationProject::init(empty.path, duplicate), MigrationError);
}
