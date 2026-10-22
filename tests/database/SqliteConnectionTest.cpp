module;
#include <filesystem>

#include "tests/UnitTestPrelude.hpp"
module orm;
import :internal;
import :connection_config;
import :sql;

namespace
{
struct SqliteConnectionFile
{
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        (std::filesystem::path{u8"orm \u017c\u00f3\u0142\u0107 = ' "} +=
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".sqlite");
    ~SqliteConnectionFile()
    {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
};
auto openSqlite(soci::session& session, const orm::SqliteConfig& config) -> void
{
    const orm::db::sqlite::SqliteBackend backend;
    backend.runtime().open(session, orm::db::ConnectionConfigView{config});
    backend.runtime().onConnect(session);
}
}

TEST(SqliteConnectionTest, preservesPathAndPerformsCrud)
{
    SqliteConnectionFile file;
    soci::session session;
    openSqlite(session, orm::SqliteConfig{.database = file.path, .openMode = orm::SqliteOpenMode::ReadWriteCreate});
    std::string actual;
    session << "SELECT file FROM pragma_database_list WHERE name='main'", soci::into(actual);
    EXPECT_TRUE(std::filesystem::equivalent(
        file.path,
        std::filesystem::path{std::u8string{reinterpret_cast<const char8_t*>(actual.data()), actual.size()}}));
    session << "CREATE TABLE test(value INTEGER)";
    session << "INSERT INTO test VALUES(42)";
    int result{};
    session << "SELECT value FROM test", soci::into(result);
    EXPECT_EQ(result, 42);
    session << "UPDATE test SET value=43";
    session << "DELETE FROM test";
    session << "SELECT COUNT(*) FROM test", soci::into(result);
    EXPECT_EQ(result, 0);
}

TEST(SqliteConnectionTest, supportsMemoryForeignKeysAndExactTimeout)
{
    for (const auto timeout : {0, 1, 1257, std::numeric_limits<int>::max()})
    {
        soci::session session;
        openSqlite(session, orm::SqliteConfig{.database = orm::SqliteMemory{},
                                              .busyTimeout = std::chrono::milliseconds{timeout}});
        int result{};
        session << "PRAGMA busy_timeout", soci::into(result);
        EXPECT_EQ(result, timeout);
        session << "PRAGMA foreign_keys", soci::into(result);
        EXPECT_EQ(result, 1);
    }
}

TEST(SqliteConnectionTest, preservesDoubleQuoteWhereTheFilesystemPermitsIt)
{
    if constexpr (std::filesystem::path::preferred_separator != '/')
    {
        GTEST_SKIP() << "Double quotes are not permitted in Windows file names";
    }
    SqliteConnectionFile file;
    file.path += "\"";
    soci::session session;
    openSqlite(session, orm::SqliteConfig{file.path});
    EXPECT_TRUE(std::filesystem::exists(file.path));
}

TEST(SqliteConnectionTest, rejectsMissingSourceNulAndInvalidTimeoutsBeforeOpening)
{
    std::vector<orm::SqliteConfig> configs{
        orm::SqliteConfig{}, orm::SqliteConfig{std::filesystem::path{":memory:"}},
        orm::SqliteConfig{std::filesystem::path{std::string{"secret\0path", 11}}},
        orm::SqliteConfig{.database = orm::SqliteMemory{}, .busyTimeout = std::chrono::milliseconds{-1}},
        orm::SqliteConfig{.database = orm::SqliteMemory{},
                          .busyTimeout =
                              std::chrono::milliseconds{static_cast<long long>(std::numeric_limits<int>::max()) + 1}},
        // Deliberately invalid input must be rejected before opening a client connection.
        // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
        orm::SqliteConfig{.database = orm::SqliteMemory{}, .openMode = static_cast<orm::SqliteOpenMode>(99)}};
    for (const auto& config : configs)
    {
        soci::session session;
        try
        {
            openSqlite(session, config);
            FAIL();
        }
        catch (const orm::DatabaseError& error)
        {
            EXPECT_EQ(error.getCode(), orm::DatabaseErrorCode::Connection);
            EXPECT_EQ(error.getOperation(), "connect");
            EXPECT_EQ(std::string{error.what()}.find("secret"), std::string::npos);
        }
        EXPECT_FALSE(session.is_connected());
    }
}

TEST(SqliteConnectionTest, honorsReadOnlyAndNoCreate)
{
    SqliteConnectionFile file;
    soci::session session;
    EXPECT_THROW(
        openSqlite(session, orm::SqliteConfig{.database = file.path, .openMode = orm::SqliteOpenMode::ReadWrite}),
        orm::DatabaseError);
    EXPECT_FALSE(std::filesystem::exists(file.path));
    openSqlite(session, orm::SqliteConfig{file.path});
    session << "CREATE TABLE test(value INTEGER)";
    session.close();
    openSqlite(session, orm::SqliteConfig{.database = file.path, .openMode = orm::SqliteOpenMode::ReadOnly});
    EXPECT_THROW((session << "INSERT INTO test VALUES(1)"), soci::soci_error);
    session.close();
    openSqlite(session, orm::SqliteConfig{.database = file.path, .openMode = orm::SqliteOpenMode::ReadWrite});
    EXPECT_NO_THROW((session << "INSERT INTO test VALUES(1)"));
}
