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
import :sql_contracts;
import :model_views;
import :migrations_schema;

export namespace orm::migrations::detail
{
struct RunnerAccess;
}

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
    IncompleteMigration,
    InvalidResolution,
    InvalidSchema,
    SessionRestoration,
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
    std::optional<std::string> nativeCode_{};
};

struct SqlMigration
{
    std::string up{};
    std::optional<std::string> down{};
    MigrationExecutionContext upContext{};
    MigrationExecutionContext downContext{};
    std::string beforeHash{};
    std::string afterHash{};
};

struct Migration
{
    Version version{};
    std::string name{};
    std::map<db::BackendType, SqlMigration> scripts{};
    SchemaSnapshot before{};
    SchemaSnapshot after{};
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
    std::vector<Migration> migrations_{};
};

struct MigrationSummary
{
    Version version{};
    std::string name{};
};

struct AppliedMigration
{
    Version version{};
    std::string name{};
    db::BackendType backend{db::BackendType::Empty};
    std::string checksum{};
    std::string appliedAt{};
    bool baseline{};
    auto operator==(const AppliedMigration&) const -> bool = default;
};

struct Diagnostic
{
    ErrorCode code{};
    Version version{};
    std::string message{};
};

enum class Direction
{
    Up,
    Down,
};

enum class Resolution
{
    Completed,
    Reverted,
};

/** Durable marker for a script whose result must be inspected by an operator. */
struct IncompleteMigration
{
    Version version{};
    std::string name{};
    db::BackendType backend{db::BackendType::Empty};
    Direction direction{Direction::Up};
    std::string checksum{};
    auto operator==(const IncompleteMigration&) const -> bool = default;
};

struct Status
{
    Version currentVersion{};
    Version baselineVersion{};
    std::vector<AppliedMigration> applied{};
    std::vector<MigrationSummary> pending{};
    std::optional<IncompleteMigration> incomplete{};
    std::vector<Diagnostic> problems{};
    [[nodiscard]] auto isValid() const noexcept -> bool
    {
        return problems.empty() && !incomplete;
    }
};

struct PlannedMigration
{
    Version version{};
    std::string name{};
    std::string sql{};
};

struct Plan
{
    Direction direction{Direction::Up};
    Version from{};
    Version to{};
    db::MigrationAtomicity atomicity{db::MigrationAtomicity::WholeMigration};
    std::vector<PlannedMigration> migrations{};
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
    /** Operator assertion after manually completing or reverting an incomplete script. */
    auto resolve(Version version, std::string_view expectedChecksum, Resolution resolution) -> void;

private:
    friend struct detail::RunnerAccess;
    Runner(Database& database, Catalog catalog, const void* operations);
    Database* database_;
    Catalog catalog_;
    const void* operations_;
    auto execute(Direction direction, std::optional<Version> target) -> void;
};

/** Commands use ORM_CXX_DATABASE_URL. args excludes the executable name. */
struct GeneratedMigration
{
    Migration migration;
    SchemaDiff diff;
};
[[nodiscard]] auto emptySnapshot(std::span<const db::BackendType> backends = generationBackends) -> SchemaSnapshot;
[[nodiscard]] auto capture(model::SchemaView models,
                           std::span<const db::BackendType> backends = generationBackends) -> SchemaSnapshot;
[[nodiscard]] auto compare(const SchemaSnapshot& before, const SchemaSnapshot& after,
                           const GenerationDecisions& decisions = {}) -> SchemaDiff;
[[nodiscard]] auto generate(Version version, std::string name, const SchemaDiff& diff,
                            const GenerationDecisions& decisions = {}) -> GeneratedMigration;

class MigrationProject final
{
public:
    explicit MigrationProject(std::filesystem::path directory);
    [[nodiscard]] static auto init(const std::filesystem::path& directory,
                                   std::span<const db::BackendType> backends = generationBackends) -> MigrationProject;
    [[nodiscard]] auto directory() const noexcept -> const std::filesystem::path&;
    [[nodiscard]] auto backends() const noexcept -> std::span<const db::BackendType>;
    [[nodiscard]] auto latestSnapshot() const -> SchemaSnapshot;
    [[nodiscard]] auto draftVersion() const -> std::optional<Version>;
    [[nodiscard]] auto catalog() const -> Catalog;
    [[nodiscard]] auto compare(model::SchemaView models, const GenerationDecisions& decisions = {}) const -> SchemaDiff;
    auto make(std::string name, model::SchemaView models, const GenerationDecisions& decisions = {}) -> Version;
    auto makeManual(std::string name, model::SchemaView models) -> Version;
    auto finalize(Version version, model::SchemaView models, bool acceptSchema) -> void;

private:
    std::filesystem::path directory_;
    std::vector<db::BackendType> backends_{};
};
auto runCommandLine(std::span<const std::string_view> args, model::SchemaView models,
                    const std::filesystem::path& directory, std::ostream& output, std::ostream& errors) -> int;
auto runCommandLine(int argc, const char* const* argv, model::SchemaView models,
                    const std::filesystem::path& directory = "migrations") -> int;
} // namespace orm::migrations
