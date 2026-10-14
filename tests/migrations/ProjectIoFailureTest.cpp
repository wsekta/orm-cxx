module;

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sys/resource.h>
#include <system_error>

#include "tests/UnitTestPrelude.hpp"

module orm;

import :internal;
import :migrations_internal;

namespace migration_project_io_models
{
struct User
{
    int id;
    std::string name;
    inline static constexpr orm::reflection::FixedString table_name{"project_io_users"};
};
using Schema = orm::Schema<User>;
} // namespace migration_project_io_models

namespace
{
using namespace orm::migrations;
using Backend = orm::db::BackendType;
using Schema = migration_project_io_models::Schema;
constexpr std::array sqliteOnly{Backend::Sqlite};

class ProjectDirectory final
{
public:
    ProjectDirectory()
        : root{std::filesystem::temp_directory_path() /
               ("orm_project_io_" +
                std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()))}
    {
        std::filesystem::create_directory(root);
    }
    ~ProjectDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
    auto projectPath() const -> std::filesystem::path
    {
        return root / "project";
    }
    auto read(const std::filesystem::path& relative) const -> std::string
    {
        std::ifstream input{projectPath() / relative, std::ios::binary};
        input.exceptions(std::ios::failbit | std::ios::badbit);
        return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    }
    std::filesystem::path root;
};

/** Restore the process resource limit before assertions and coverage output. */
class RejectFileWrites final
{
public:
    RejectFileWrites()
    {
        if (getrlimit(RLIMIT_FSIZE, &previousLimit_) != 0)
            throw std::system_error{errno, std::generic_category(), "Cannot inspect file size limit"};
        struct sigaction ignore
        {
        };
        ignore.sa_handler = SIG_IGN;
        if (sigemptyset(&ignore.sa_mask) != 0 || sigaction(SIGXFSZ, &ignore, &previousSignal_) != 0)
            throw std::system_error{errno, std::generic_category(), "Cannot ignore file size signal"};
        auto limit = previousLimit_;
        limit.rlim_cur = 0;
        if (setrlimit(RLIMIT_FSIZE, &limit) != 0)
        {
            const auto error = errno;
            if (sigaction(SIGXFSZ, &previousSignal_, nullptr) != 0)
                std::abort();
            throw std::system_error{error, std::generic_category(), "Cannot limit file writes"};
        }
    }
    RejectFileWrites(const RejectFileWrites&) = delete;
    auto operator=(const RejectFileWrites&) -> RejectFileWrites& = delete;
    ~RejectFileWrites()
    {
        if (setrlimit(RLIMIT_FSIZE, &previousLimit_) != 0 || sigaction(SIGXFSZ, &previousSignal_, nullptr) != 0)
            std::abort();
    }

private:
    struct rlimit previousLimit_
    {
    };
    struct sigaction previousSignal_
    {
    };
};

template <typename Function>
auto rejectedWrite(Function&& operation) -> std::optional<std::pair<ErrorCode, std::string>>
{
    RejectFileWrites limit;
    try
    {
        operation();
        return std::nullopt;
    }
    catch (const MigrationError& error)
    {
        return std::pair{error.getCode(), std::string{error.what()}};
    }
}
} // namespace

TEST(MigrationProjectIoFailureTest, FailedInitializationRemovesTemporaryMetadataAndReleasesWriterLock)
{
    ProjectDirectory directory;
    const auto error = rejectedWrite([&] { (void)MigrationProject::init(directory.projectPath(), sqliteOnly); });
    ASSERT_TRUE(error);
    EXPECT_EQ(error->first, ErrorCode::InvalidCatalog);
    EXPECT_NE(error->second.find("Cannot write migration project files"), std::string::npos);
    EXPECT_TRUE(std::filesystem::is_empty(directory.projectPath()));
    EXPECT_FALSE(std::filesystem::exists(directory.projectPath() / ".project.json.tmp"));
    EXPECT_FALSE(std::filesystem::exists(directory.projectPath() / "project.json"));
    EXPECT_FALSE(std::filesystem::exists(directory.projectPath() / ".orm-cxx-lock"));
    EXPECT_NO_THROW((void)MigrationProject::init(directory.projectPath(), sqliteOnly));
}

TEST(MigrationProjectIoFailureTest, FailedDraftWriteCannotPublishAnIncompleteMigration)
{
    ProjectDirectory directory;
    auto project = MigrationProject::init(directory.projectPath(), sqliteOnly);
    const auto configuration = directory.read("project.json");
    const auto error = rejectedWrite([&] { (void)project.make("initial", Schema::view); });
    ASSERT_TRUE(error);
    EXPECT_EQ(error->first, ErrorCode::InvalidCatalog);
    EXPECT_EQ(directory.read("project.json"), configuration);
    EXPECT_FALSE(std::filesystem::exists(directory.projectPath() / ".orm-cxx-staging"));
    EXPECT_FALSE(std::filesystem::exists(directory.projectPath() / ".orm-cxx-lock"));
    EXPECT_FALSE(std::filesystem::exists(directory.projectPath() / "1_initial"));
    EXPECT_FALSE(project.draftVersion());
    EXPECT_EQ(project.make("initial", Schema::view), 1);
}

TEST(MigrationProjectIoFailureTest, FailedReadyMarkerPreservesDraftBytesAndCanBeRetried)
{
    ProjectDirectory directory;
    auto project = MigrationProject::init(directory.projectPath(), sqliteOnly);
    ASSERT_EQ(project.make("initial", Schema::view), 1);
    const auto metadata = directory.read("1_initial/migration.json");
    const auto snapshot = directory.read("1_initial/schema.json");
    const auto script = directory.read("1_initial/sqlite/up.sql");
    const auto error = rejectedWrite([&] { project.finalize(1, Schema::view, true); });
    ASSERT_TRUE(error);
    EXPECT_EQ(error->first, ErrorCode::InvalidCatalog);
    EXPECT_NE(error->second.find("Cannot write migration project files"), std::string::npos);
    EXPECT_EQ(directory.read("1_initial/migration.json"), metadata);
    EXPECT_EQ(directory.read("1_initial/schema.json"), snapshot);
    EXPECT_EQ(directory.read("1_initial/sqlite/up.sql"), script);
    EXPECT_FALSE(std::filesystem::exists(directory.projectPath() / "1_initial/.ready.json.tmp"));
    EXPECT_FALSE(std::filesystem::exists(directory.projectPath() / "1_initial/ready.json"));
    EXPECT_FALSE(std::filesystem::exists(directory.projectPath() / ".orm-cxx-lock"));
    EXPECT_EQ(project.draftVersion(), 1);
    EXPECT_THROW((void)project.catalog(), MigrationError);
    EXPECT_NO_THROW(project.finalize(1, Schema::view, true));
    EXPECT_EQ(project.catalog().migrations().size(), 1);
}
