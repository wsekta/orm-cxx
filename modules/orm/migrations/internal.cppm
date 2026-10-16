module;

#include <functional>
#include <memory>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../../detail/SociTypes.inc"

module orm:migrations_internal;

import :migrations;
import :migrations_schema;
import :model_views;
import :foundation;
import :sql;
import :database;

namespace orm::migrations::detail
{
/** Private session dependency; injected instances must outlive their Runner. */
class SessionOperations
{
public:
    virtual ~SessionOperations() = default;
    virtual auto commit(soci::session& session, Version version) const -> void;
    virtual auto rollback(soci::session& session) const -> void;
    virtual auto executeScript(soci::session& session, const db::BackendRuntime& runtime, std::string_view sql,
                               Version version) const -> void;
    virtual auto restoreSqliteForeignKeys(soci::session& session, int previousValue) const -> void;
};

/** Owns the durable state protocol. Schema checks stay with the Runner. */
class MigrationState
{
public:
    MigrationState(Database& database, const Catalog& catalog, const SessionOperations& operations);
    [[nodiscard]] auto status() const -> Status;
    auto validate() const -> void;
    [[nodiscard]] auto plan(const Status& initial, Direction direction, std::optional<Version> target) const -> Plan;
    [[nodiscard]] auto apply(const Migration& migration, Direction direction,
                             const std::vector<AppliedMigration>& expected, const std::function<void()>& preflight,
                             const std::function<void()>& validateResult) -> std::vector<AppliedMigration>;
    auto baseline(Version target) -> void;
    auto resolve(Version version, std::string_view expectedChecksum, Resolution resolution) -> void;

private:
    Database& database_;
    const Catalog& catalog_;
    const SessionOperations& operations_;
    db::BackendType backend_;
    const db::BackendRuntime& runtime_;
    soci::session& session_;
    std::string table_;

    [[nodiscard]] auto inspect() const -> Status;
    auto requireHistory(const std::vector<AppliedMigration>& expected, Version version) const -> void;
    auto requireMarker(const IncompleteMigration& expected, std::string_view message) const -> void;
    auto changeHistory(const Migration& migration, Direction direction) -> void;
    auto clearMarker(Version version) -> void;
};

struct RunnerAccess
{
    static auto withSessionOperations(Database& database, Catalog catalog,
                                      const SessionOperations& operations) -> Runner
    {
        return Runner{database, std::move(catalog), std::addressof(operations)};
    }
};

struct DatabaseAccess
{
    static auto session(Database& database) -> soci::session&
    {
        return database.sql;
    }
    static auto runtime(Database& database) -> const db::BackendRuntime&
    {
        return database.getBackend().runtime();
    }
    static auto busy(const Database& database) -> bool
    {
        return database.transaction != nullptr || database.migrationActive;
    }
    static auto setActive(Database& database, bool active) -> void
    {
        database.migrationActive = active;
    }
    static auto active(const Database& database) -> bool
    {
        return database.migrationActive;
    }
};

[[nodiscard]] auto normalizeSql(std::string_view sql) -> std::string;
[[nodiscard]] auto sha256(std::string_view bytes) -> std::string;
[[nodiscard]] auto checksum(const SqlMigration& scripts) -> std::string;
[[nodiscard]] auto snapshotJson(const SchemaSnapshot& snapshot) -> std::string;
[[nodiscard]] auto snapshotFromJson(std::string_view json) -> SchemaSnapshot;
[[nodiscard]] auto contextJson(const MigrationExecutionContext& context) -> std::string;
[[nodiscard]] auto contextFromJson(std::string_view json) -> MigrationExecutionContext;
[[nodiscard]] auto snapshotHash(const SchemaSnapshot& snapshot, db::BackendType backend) -> std::string;
[[nodiscard]] auto decisionsFromJson(std::string_view json) -> GenerationDecisions;
[[nodiscard]] auto quoteIdentifier(db::BackendType backend, std::string_view name) -> std::string;
[[nodiscard]] auto renderColumn(db::BackendType backend, const PhysicalColumn& column, bool inlinePrimary = false,
                                std::string_view primaryName = {}) -> std::string;
[[nodiscard]] auto renderCreateTable(db::BackendType backend, const PhysicalTable& table,
                                     bool includeForeignKeys = true, bool ifNotExists = false) -> std::string;
[[nodiscard]] auto renderForeignKey(db::BackendType backend, const PhysicalForeignKey& key) -> std::string;
[[nodiscard]] auto physicalTable(model::ModelView model, db::BackendType backend) -> PhysicalTable;
[[nodiscard]] auto physicalJunctions(model::ModelView model, db::BackendType backend) -> std::vector<PhysicalTable>;
[[nodiscard]] auto physicalTable(model::ModelView model, const db::SqlDialect& dialect) -> PhysicalTable;
[[nodiscard]] auto physicalJunctions(model::ModelView model,
                                     const db::SqlDialect& dialect) -> std::vector<PhysicalTable>;
[[nodiscard]] auto renderCreateTable(const db::SqlDialect& dialect, const PhysicalTable& table,
                                     bool includeForeignKeys = true, bool ifNotExists = false) -> std::string;
auto canonicalize(SchemaSnapshot& snapshot) -> void;
auto validateSnapshot(const SchemaSnapshot& snapshot) -> void;
auto validateScript(std::string_view script, db::BackendType backend, Version version) -> void;
[[nodiscard]] auto backendName(db::BackendType backend) -> std::string_view;
auto runCatalogCommandWithConnection(std::span<const std::string_view> args, const Catalog& catalog,
                                     std::optional<std::string_view> connection, std::ostream& output,
                                     std::ostream& errors) -> int;
} // namespace orm::migrations::detail
