module;

#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <map>
#include <optional>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

export module orm:migrations;

import :foundation;
import :database;

/** Versioned SQL migrations, independent of the application's current model types. */
export namespace orm::migrations
{
using Version = std::int64_t;

enum class ErrorCode
{
    InvalidCatalog,
    InvalidSql,
    NotConnected,
    ActiveTransaction,
    UnsupportedBackend,
    InvalidHistory,
    ChecksumMismatch,
    InvalidTarget,
    MissingDown,
    BaselineBoundary,
    LockUnavailable,
    HistoryChanged,
    Execution,
    CommitUncertain,
};

class MigrationError final : public std::runtime_error
{
public:
    MigrationError(ErrorCode code, std::string message, Version version = 0,
                   db::BackendType backend = db::BackendType::Empty,
                   std::optional<std::string> nativeCode = std::nullopt);
    [[nodiscard]] auto getCode() const noexcept -> ErrorCode;
    [[nodiscard]] auto getVersion() const noexcept -> Version;
    [[nodiscard]] auto getBackendType() const noexcept -> db::BackendType;
    [[nodiscard]] auto getNativeCode() const noexcept -> const std::optional<std::string>&;

private:
    ErrorCode code_;
    Version version_;
    db::BackendType backend_;
    std::optional<std::string> nativeCode_;
};

struct SqlMigration
{
    std::string up;
    std::optional<std::string> down;
};

struct Migration
{
    Version version{};
    std::string name;
    std::map<db::BackendType, SqlMigration> scripts;
};

/** Owns normalized SQL. Loading does not connect to or modify a database. */
class Catalog final
{
public:
    explicit Catalog(std::vector<Migration> migrations = {});
    Catalog(std::initializer_list<Migration> migrations);
    [[nodiscard]] static auto fromDirectory(const std::filesystem::path& directory) -> Catalog;
    [[nodiscard]] auto migrations() const noexcept -> std::span<const Migration>;

private:
    std::vector<Migration> migrations_;
};

struct MigrationSummary
{
    Version version{};
    std::string name;
};

struct AppliedMigration
{
    Version version{};
    std::string name;
    db::BackendType backend{db::BackendType::Empty};
    std::string checksum;
    std::string appliedAt;
    bool baseline{};
    auto operator==(const AppliedMigration&) const -> bool = default;
};

struct Diagnostic
{
    ErrorCode code{};
    Version version{};
    std::string message;
};

struct Status
{
    Version currentVersion{};
    Version baselineVersion{};
    std::vector<AppliedMigration> applied;
    std::vector<MigrationSummary> pending;
    std::vector<Diagnostic> problems;
    [[nodiscard]] auto isValid() const noexcept -> bool
    {
        return problems.empty();
    }
};

enum class Direction
{
    Up,
    Down,
};

struct PlannedMigration
{
    Version version{};
    std::string name;
    std::string sql;
};

struct Plan
{
    Direction direction{Direction::Up};
    Version from{};
    Version to{};
    std::vector<PlannedMigration> migrations;
};

/** Borrows an idle connected Database; the Database must outlive this runner. */
class Runner final
{
public:
    Runner(Database& database, Catalog catalog);
    [[nodiscard]] auto status() const -> Status;
    auto validate() const -> void;
    [[nodiscard]] auto preview(Direction direction, std::optional<Version> target = std::nullopt) const -> Plan;
    auto up(std::optional<Version> target = std::nullopt) -> void;
    auto down(Version target) -> void;
    auto baseline(Version target) -> void;

private:
    Database* database_;
    Catalog catalog_;
    auto execute(Direction direction, std::optional<Version> target) -> void;
};

/** Commands use ORM_CXX_DATABASE_URL. args excludes the executable name. */
auto runCommandLine(std::span<const std::string_view> args, const Catalog& catalog, std::ostream& output,
                    std::ostream& errors) -> int;
auto runCommandLine(int argc, const char* const* argv, const Catalog& catalog) -> int;
} // namespace orm::migrations
