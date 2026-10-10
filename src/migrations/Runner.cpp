module;

#include <algorithm>
#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <typeinfo>
#include <utility>
#include <vector>

#include "soci/soci.h"

module orm;

import :migrations_internal;

namespace orm::migrations
{
namespace
{
using Access = detail::DatabaseAccess;

auto ready(Database& database) -> db::BackendType
{
    const auto backend = database.getBackendType();
    if (!database.isConnected())
    {
        throw MigrationError{ErrorCode::NotConnected, "Migrations require a connected database", 0, backend};
    }
    if (Access::busy(database))
    {
        throw MigrationError{ErrorCode::ActiveTransaction, "Migrations require an idle database session", 0, backend};
    }
    if (!database.getBackendCapabilities().schema.transactionalMigrations)
    {
        throw MigrationError{ErrorCode::UnsupportedBackend, "Backend does not support transactional migrations", 0,
                             backend};
    }
    (void)detail::backendName(backend);
    return backend;
}
auto scriptsFor(const Migration& migration, db::BackendType backend) -> const SqlMigration&
{
    const auto found = migration.scripts.find(backend);
    if (found == migration.scripts.end())
    {
        throw MigrationError{ErrorCode::InvalidCatalog, "Migration has no SQL for this backend", migration.version,
                             backend};
    }
    return found->second;
}
auto validateCatalog(const Catalog& catalog, db::BackendType backend) -> void
{
    for (const auto& migration : catalog.migrations())
    {
        const auto& scripts = scriptsFor(migration, backend);
        detail::validateScript(scripts.up, backend, migration.version);
        if (scripts.down)
        {
            detail::validateScript(*scripts.down, backend, migration.version);
        }
    }
}

class Transaction
{
public:
    Transaction(Database& database, std::string_view table, bool writing) : database_{database}
    {
        if (writing)
        {
            Access::runtime(database).beginMigration(Access::session(database), table);
        }
        else
        {
            Access::session(database).begin();
        }
        Access::setActive(database, true);
    }
    Transaction(const Transaction&) = delete;
    auto operator=(const Transaction&) -> Transaction& = delete;
    ~Transaction()
    {
        if (!finished_)
        {
            try
            {
                Access::session(database_).rollback();
            }
            catch (...)
            {
                // No application transaction or pooled SOCI session is active here.
                database_.disconnect();
            }
        }
        Access::setActive(database_, false);
    }
    auto commit(Version version) -> void
    {
        const auto backend = database_.getBackendType();
        try
        {
            Access::session(database_).commit();
            finished_ = true;
        }
        catch (...)
        {
            // Do not retry a potentially committed migration using this session.
            database_.disconnect();
            finished_ = true;
            throw MigrationError{ErrorCode::CommitUncertain,
                                 "Migration commit outcome is uncertain; reconnect and inspect history", version,
                                 backend};
        }
    }

private:
    Database& database_;
    bool finished_ = false;
};

auto readHistory(soci::session& session, const std::string& table) -> std::vector<AppliedMigration>
{
    // Qualified table discovery uses the same resolved namespace as history reads/writes.
    int exists = 0;
    if (session.get_backend_name() == "sqlite3")
    {
        session << "SELECT COUNT(*) FROM main.sqlite_master WHERE type='table' AND name='_orm_migrations'",
            soci::into(exists);
    }
    else
    {
        session << "SELECT CASE WHEN to_regclass(:table) IS NULL THEN 0 ELSE 1 END", soci::use(table, "table"),
            soci::into(exists);
    }
    if (!exists)
    {
        return {};
    }
    std::vector<AppliedMigration> history;
    soci::rowset<soci::row> rows =
        (session.prepare << "SELECT version,name,backend,checksum,applied_at,baseline FROM " + table +
                                " ORDER BY version");
    try
    {
        for (const auto& row : rows)
        {
            const auto flag = row.get<int>(5);
            const auto backend = row.get<int>(2);
            if ((flag != 0 && flag != 1) || (backend != static_cast<int>(db::BackendType::Sqlite) &&
                                             backend != static_cast<int>(db::BackendType::Postgres)))
            {
                throw MigrationError{ErrorCode::InvalidHistory, "Invalid migration history record"};
            }
            // SQLite reports its BIGINT values as long long; PostgreSQL BIGINT does too.
            history.push_back(AppliedMigration{.version = row.get<long long>(0),
                                               .name = row.get<std::string>(1),
                                               .backend = static_cast<db::BackendType>(backend),
                                               .checksum = row.get<std::string>(3),
                                               .appliedAt = row.get<std::string>(4),
                                               .baseline = flag != 0});
        }
    }
    catch (const std::bad_cast&)
    {
        throw MigrationError{ErrorCode::InvalidHistory, "Migration history has incompatible column types"};
    }
    return history;
}

auto evaluate(const Catalog& catalog, db::BackendType backend, std::vector<AppliedMigration> history) -> Status
{
    Status result{.currentVersion = history.empty() ? 0 : history.back().version,
                  .baselineVersion = 0,
                  .applied = std::move(history),
                  .pending = {},
                  .problems = {}};
    const auto migrations = catalog.migrations();
    bool ordinarySeen = false;
    for (std::size_t i = 0; i < result.applied.size(); ++i)
    {
        const auto& record = result.applied[i];
        if (record.baseline)
        {
            result.baselineVersion = record.version;
        }
        if ((ordinarySeen && record.baseline) || i >= migrations.size() || record.version != migrations[i].version ||
            record.name != migrations[i].name || record.backend != backend)
        {
            result.problems.push_back(
                {ErrorCode::InvalidHistory, record.version, "Migration history is not a matching catalog prefix"});
        }
        else if (record.checksum != detail::checksum(scriptsFor(migrations[i], backend)))
        {
            result.problems.push_back(
                {ErrorCode::ChecksumMismatch, record.version, "Applied migration SQL has changed"});
        }
        ordinarySeen = ordinarySeen || !record.baseline;
    }
    for (std::size_t i = result.applied.size(); i < migrations.size(); ++i)
    {
        result.pending.push_back({migrations[i].version, migrations[i].name});
    }
    return result;
}
auto requireValid(const Status& status, db::BackendType backend) -> void
{
    if (!status.isValid())
    {
        const auto& issue = status.problems.front();
        throw MigrationError{issue.code, issue.message, issue.version, backend};
    }
}

auto makePlan(const Catalog& catalog, db::BackendType backend, const Status& status, Direction direction,
              std::optional<Version> requested) -> Plan
{
    requireValid(status, backend);
    const auto migrations = catalog.migrations();
    const auto target =
        requested.value_or(direction == Direction::Up && !migrations.empty() ? migrations.back().version : 0);
    if (target < 0 || (target != 0 && std::ranges::find(migrations, target, &Migration::version) == migrations.end()) ||
        (direction == Direction::Up && target < status.currentVersion) ||
        (direction == Direction::Down && target > status.currentVersion))
    {
        throw MigrationError{ErrorCode::InvalidTarget, "Invalid migration target", target, backend};
    }
    if (direction == Direction::Down && target < status.baselineVersion)
    {
        throw MigrationError{ErrorCode::BaselineBoundary, "Cannot migrate below the baseline", target, backend};
    }
    Plan result{.direction = direction, .from = status.currentVersion, .to = target, .migrations = {}};
    for (const auto& migration : migrations)
    {
        const bool selected = direction == Direction::Up ?
                                  migration.version > status.currentVersion && migration.version <= target :
                                  migration.version <= status.currentVersion && migration.version > target;
        if (!selected)
        {
            continue;
        }
        const auto& scripts = scriptsFor(migration, backend);
        if (direction == Direction::Down && !scripts.down)
        {
            throw MigrationError{ErrorCode::MissingDown, "Migration has no down SQL", migration.version, backend};
        }
        result.migrations.push_back(
            {migration.version, migration.name, direction == Direction::Up ? scripts.up : *scripts.down});
    }
    if (direction == Direction::Down)
    {
        std::ranges::reverse(result.migrations);
    }
    return result;
}

auto createHistory(soci::session& session, const std::string& table) -> void
{
    session << "CREATE TABLE IF NOT EXISTS " + table +
                   " (version BIGINT PRIMARY KEY NOT NULL, name TEXT NOT NULL, backend INTEGER NOT NULL, checksum TEXT "
                   "NOT NULL, applied_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP, baseline INTEGER NOT NULL "
                   "CHECK(baseline IN (0,1)))";
}
auto recordHistory(soci::session& session, const std::string& table, const Migration& migration,
                   db::BackendType backend, bool baseline) -> void
{
    const auto checksum = detail::checksum(scriptsFor(migration, backend));
    const auto version = static_cast<long long>(migration.version);
    const auto backendNumber = static_cast<int>(backend);
    const int flag = baseline ? 1 : 0;
    session << "INSERT INTO " + table +
                   " (version,name,backend,checksum,baseline) VALUES (:version,:name,:backend,:checksum,:baseline)",
        soci::use(version, "version"), soci::use(migration.name, "name"), soci::use(backendNumber, "backend"),
        soci::use(checksum, "checksum"), soci::use(flag, "baseline");
}
[[noreturn]] auto databaseFailure(const soci::soci_error& error, const db::BackendRuntime& runtime,
                                  db::BackendType backend, Version version) -> void
{
    const auto translated = runtime.translateError(error, DatabaseErrorCode::Statement, "migrations");
    throw MigrationError{ErrorCode::Execution, "Migration SQL or history operation failed", version, backend,
                         translated.getNativeCode()};
}
} // namespace

Runner::Runner(Database& database, Catalog catalog) : database_{&database}, catalog_{std::move(catalog)} {}

auto Runner::status() const -> Status
{
    const auto backend = ready(*database_);
    validateCatalog(catalog_, backend);
    const auto& runtime = Access::runtime(*database_);
    try
    {
        auto& session = Access::session(*database_);
        const auto table = runtime.migrationTable(session);
        Transaction transaction{*database_, table, false};
        auto result = evaluate(catalog_, backend, readHistory(session, table));
        transaction.commit(0);
        return result;
    }
    catch (const soci::soci_error& error)
    {
        databaseFailure(error, runtime, backend, 0);
    }
    catch (const MigrationError& error)
    {
        throw MigrationError{error.getCode(), error.what(), error.getVersion(), backend, error.getNativeCode()};
    }
}
auto Runner::validate() const -> void
{
    requireValid(status(), database_->getBackendType());
}
auto Runner::preview(Direction direction, std::optional<Version> target) const -> Plan
{
    return makePlan(catalog_, database_->getBackendType(), status(), direction, target);
}
auto Runner::up(std::optional<Version> target) -> void
{
    execute(Direction::Up, target);
}
auto Runner::down(Version target) -> void
{
    execute(Direction::Down, target);
}

auto Runner::execute(Direction direction, std::optional<Version> target) -> void
{
    const auto initial = status();
    const auto backend = database_->getBackendType();
    const auto plan = makePlan(catalog_, backend, initial, direction, target);
    auto expected = initial.applied;
    auto& session = Access::session(*database_);
    const auto& runtime = Access::runtime(*database_);
    const auto table = runtime.migrationTable(session);
    for (const auto& step : plan.migrations)
    {
        try
        {
            Transaction transaction{*database_, table, true};
            if (readHistory(session, table) != expected)
            {
                throw MigrationError{ErrorCode::HistoryChanged, "Migration history changed; inspect and retry",
                                     step.version, backend};
            }
            createHistory(session, table);
            runtime.executeMigrationScript(session, step.sql);
            if (direction == Direction::Up)
            {
                const auto migration = std::ranges::find(catalog_.migrations(), step.version, &Migration::version);
                recordHistory(session, table, *migration, backend, false);
            }
            else
            {
                const auto version = static_cast<long long>(step.version);
                session << "DELETE FROM " + table + " WHERE version=:version", soci::use(version, "version");
            }
            auto actual = readHistory(session, table);
            const auto wantedSize = direction == Direction::Up ? expected.size() + 1 : expected.size() - 1;
            const auto preservedSize = direction == Direction::Up ? expected.size() : wantedSize;
            if (actual.size() != wantedSize ||
                !std::equal(expected.begin(), expected.begin() + static_cast<std::ptrdiff_t>(preservedSize),
                            actual.begin()))
            {
                throw MigrationError{ErrorCode::InvalidHistory, "Migration script modified its history", step.version,
                                     backend};
            }
            expected = std::move(actual);
            // Scripts may not erase or corrupt the migration ledger.
            const auto next = evaluate(catalog_, backend, expected);
            requireValid(next, backend);
            transaction.commit(step.version);
        }
        catch (const soci::soci_error& error)
        {
            databaseFailure(error, runtime, backend, step.version);
        }
        catch (const MigrationError& error)
        {
            throw MigrationError{error.getCode(), error.what(), step.version, backend, error.getNativeCode()};
        }
    }
}

auto Runner::baseline(Version target) -> void
{
    const auto initial = status();
    const auto backend = database_->getBackendType();
    requireValid(initial, backend);
    if (!initial.applied.empty() || target <= 0)
    {
        throw MigrationError{ErrorCode::InvalidTarget, "Baseline requires empty history and a positive target", target,
                             backend};
    }
    const auto plan = makePlan(catalog_, backend, initial, Direction::Up, target);
    const auto& runtime = Access::runtime(*database_);
    try
    {
        auto& session = Access::session(*database_);
        const auto table = runtime.migrationTable(session);
        Transaction transaction{*database_, table, true};
        if (!readHistory(session, table).empty())
        {
            throw MigrationError{ErrorCode::HistoryChanged, "Migration history changed; inspect and retry", target,
                                 backend};
        }
        createHistory(session, table);
        for (const auto& migration : catalog_.migrations())
        {
            if (migration.version > target)
            {
                break;
            }
            recordHistory(session, table, migration, backend, true);
        }
        const auto recorded = evaluate(catalog_, backend, readHistory(session, table));
        requireValid(recorded, backend);
        if (recorded.applied.size() != plan.migrations.size() || recorded.currentVersion != target ||
            recorded.baselineVersion != target)
        {
            throw MigrationError{ErrorCode::InvalidHistory, "Baseline history was not recorded as expected", target,
                                 backend};
        }
        transaction.commit(target);
    }
    catch (const soci::soci_error& error)
    {
        databaseFailure(error, runtime, backend, target);
    }
    catch (const MigrationError& error)
    {
        throw MigrationError{error.getCode(), error.what(), target, backend, error.getNativeCode()};
    }
}
} // namespace orm::migrations
