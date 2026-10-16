module;

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <exception>
#include <format>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <sstream>
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
[[noreturn]] auto databaseFailure(const soci::soci_error& error, const db::BackendRuntime& runtime,
                                  db::BackendType backend, Version version) -> void;

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
    if (!database.getBackendCapabilities().migrations.supported)
    {
        throw MigrationError{ErrorCode::UnsupportedBackend, "Backend does not support migrations", 0, backend};
    }
    (void)detail::backendName(backend);
    const auto& runtime = Access::runtime(database);
    try
    {
        runtime.validateMigrationSession(Access::session(database));
    }
    catch (const soci::soci_error& error)
    {
        databaseFailure(error, runtime, backend, 0);
    }
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
    Transaction(Database& database, std::string_view table, bool writing, const detail::SessionOperations& operations)
        : database_{database}, operations_{operations}, previouslyActive_{Access::active(database)}
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
                operations_.rollback(Access::session(database_));
            }
            catch (...)
            {
                // No application transaction or pooled SOCI session is active here.
                database_.disconnect();
            }
        }
        Access::setActive(database_, previouslyActive_);
    }
    auto commit(Version version) -> void
    {
        const auto backend = database_.getBackendType();
        try
        {
            operations_.commit(Access::session(database_), version);
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
    const detail::SessionOperations& operations_;
    bool finished_ = false;
    bool previouslyActive_ = false;
};

/** A nontransactional backend's session lock survives the script's implicit commits. */
class MigrationLock
{
public:
    MigrationLock(Database& database, std::string_view table) : database_{database}, table_{table}
    {
        Access::runtime(database_).beginMigration(Access::session(database_), table_);
        Access::setActive(database_, true);
    }
    MigrationLock(const MigrationLock&) = delete;
    auto operator=(const MigrationLock&) -> MigrationLock& = delete;
    ~MigrationLock()
    {
        try
        {
            if (!database_.isConnected())
            {
                database_.disconnect();
            }
            else if (!finished_)
            {
                Access::runtime(database_).endMigration(Access::session(database_), table_);
            }
        }
        catch (...)
        {
            try
            {
                database_.disconnect();
            }
            catch (...)
            {
            }
        }
        Access::setActive(database_, false);
    }
    auto finish() -> void
    {
        Access::runtime(database_).endMigration(Access::session(database_), table_);
        finished_ = true;
        Access::setActive(database_, false);
    }

private:
    Database& database_;
    std::string table_;
    bool finished_ = false;
};

auto readHistory(soci::session& session, const db::BackendRuntime& runtime,
                 const std::string& table) -> std::vector<AppliedMigration>
{
    if (!runtime.migrationHistoryExists(session, table))
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
                                             backend != static_cast<int>(db::BackendType::Postgres) &&
                                             backend != static_cast<int>(db::BackendType::Mysql) &&
                                             backend != static_cast<int>(db::BackendType::Mariadb)))
            {
                throw MigrationError{ErrorCode::InvalidHistory, "Invalid migration history record"};
            }
            std::string appliedAt;
            if (row.get_properties(4).get_data_type() == soci::dt_date)
            {
                const auto timestamp = row.get<std::tm>(4);
                std::ostringstream text;
                text << std::put_time(&timestamp, "%Y-%m-%d %H:%M:%S");
                appliedAt = text.str();
            }
            else
            {
                appliedAt = row.get<std::string>(4);
            }
            history.push_back(AppliedMigration{.version = row.get<long long>(0),
                                               .name = row.get<std::string>(1),
                                               .backend = static_cast<db::BackendType>(backend),
                                               .checksum = row.get<std::string>(3),
                                               .appliedAt = std::move(appliedAt),
                                               .baseline = flag != 0});
        }
    }
    catch (const std::bad_cast&)
    {
        throw MigrationError{ErrorCode::InvalidHistory, "Migration history has incompatible column types"};
    }
    return history;
}

auto readIncomplete(soci::session& session, const db::BackendRuntime& runtime,
                    const std::string& table) -> std::optional<IncompleteMigration>
{
    const auto journal = runtime.migrationJournalTable(session, table);
    if (journal.empty() || !runtime.migrationHistoryExists(session, journal))
    {
        return std::nullopt;
    }
    std::optional<IncompleteMigration> result;
    soci::rowset<soci::row> rows =
        (session.prepare << "SELECT slot,version,name,backend,direction,checksum FROM " + journal);
    try
    {
        for (const auto& row : rows)
        {
            const auto backend = static_cast<db::BackendType>(row.get<int>(3));
            const auto direction = row.get<int>(4);
            if (result || row.get<int>(0) != 1 || (direction != 0 && direction != 1) ||
                (backend != db::BackendType::Mysql && backend != db::BackendType::Mariadb))
            {
                throw MigrationError{ErrorCode::InvalidHistory, "Invalid incomplete migration marker"};
            }
            result = IncompleteMigration{.version = row.get<long long>(1),
                                         .name = row.get<std::string>(2),
                                         .backend = backend,
                                         .direction = direction == 0 ? Direction::Up : Direction::Down,
                                         .checksum = row.get<std::string>(5)};
        }
    }
    catch (const std::bad_cast&)
    {
        throw MigrationError{ErrorCode::InvalidHistory, "Incomplete marker has incompatible column types"};
    }
    return result;
}

auto evaluate(const Catalog& catalog, db::BackendType backend, std::vector<AppliedMigration> history,
              std::optional<IncompleteMigration> incomplete = std::nullopt) -> Status
{
    Status result{.currentVersion = history.empty() ? 0 : history.back().version,
                  .baselineVersion = 0,
                  .applied = std::move(history),
                  .pending = {},
                  .incomplete = std::move(incomplete),
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
    if (result.incomplete)
    {
        const auto& marker = *result.incomplete;
        const auto found = std::ranges::find(migrations, marker.version, &Migration::version);
        const auto expectedIndex = marker.direction == Direction::Up ?
                                       result.applied.size() :
                                       (result.applied.empty() ? migrations.size() : result.applied.size() - 1);
        if (found == migrations.end() || marker.backend != backend || marker.name != found->name ||
            expectedIndex >= migrations.size() || migrations[expectedIndex].version != marker.version ||
            (marker.direction == Direction::Down && marker.version <= result.baselineVersion))
        {
            result.problems.push_back(
                {ErrorCode::InvalidHistory, marker.version, "Incomplete marker does not match catalog position"});
        }
        else if (marker.checksum != detail::checksum(scriptsFor(*found, backend)))
        {
            result.problems.push_back(
                {ErrorCode::ChecksumMismatch, marker.version, "Incomplete migration SQL has changed"});
        }
    }
    return result;
}
auto requireValid(const Status& status, db::BackendType backend) -> void
{
    if (!status.problems.empty())
    {
        const auto& issue = status.problems.front();
        throw MigrationError{issue.code, issue.message, issue.version, backend};
    }
    if (status.incomplete)
    {
        throw MigrationError{ErrorCode::IncompleteMigration, "Incomplete migration requires operator resolution",
                             status.incomplete->version, backend};
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

auto recordIncomplete(soci::session& session, const std::string& journal, const Migration& migration,
                      db::BackendType backend, Direction direction) -> void
{
    const auto version = static_cast<long long>(migration.version);
    const auto backendNumber = static_cast<int>(backend);
    const auto directionNumber = direction == Direction::Up ? 0 : 1;
    const auto checksum = detail::checksum(scriptsFor(migration, backend));
    session << "INSERT INTO " + journal +
                   " (slot,version,name,backend,direction,checksum) VALUES "
                   "(1,:version,:name,:backend,:direction,:checksum)",
        soci::use(version, "version"), soci::use(migration.name, "name"), soci::use(backendNumber, "backend"),
        soci::use(directionNumber, "direction"), soci::use(checksum, "checksum");
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
/** Holds the engine's migration lock across all commits of one state change. */
class HistoryWrite
{
public:
    HistoryWrite(Database& database, const std::string& table, const detail::SessionOperations& operations)
        : database_{database},
          table_{table},
          operations_{operations},
          nonTransactional_{database.getBackendCapabilities().migrations.atomicity ==
                            db::MigrationAtomicity::NonTransactional}
    {
        if (nonTransactional_)
            lock_.emplace(database_, table_);
        else
            transaction_.emplace(database_, table_, true, operations_);
    }
    [[nodiscard]] auto nonTransactional() const -> bool
    {
        return nonTransactional_;
    }
    auto beginHistory() -> void
    {
        if (!transaction_)
            transaction_.emplace(database_, table_, false, operations_);
    }
    auto commit(Version version) -> void
    {
        transaction_->commit(version);
        if (lock_)
            lock_->finish();
    }

private:
    Database& database_;
    const std::string& table_;
    const detail::SessionOperations& operations_;
    bool nonTransactional_;
    // Destruction rolls back history before releasing the session lock.
    std::optional<MigrationLock> lock_;
    std::optional<Transaction> transaction_;
};

} // namespace

auto detail::SessionOperations::commit(soci::session& session, Version version) const -> void
{
    static_cast<void>(version);
    session.commit();
}

auto detail::SessionOperations::rollback(soci::session& session) const -> void
{
    session.rollback();
}

auto detail::SessionOperations::executeScript(soci::session& session, const db::BackendRuntime& runtime,
                                              std::string_view sql, Version /*version*/) const -> void
{
    runtime.executeMigrationScript(session, sql);
}

auto detail::SessionOperations::restoreSqliteForeignKeys(soci::session& session, int previousValue) const -> void
{
    session << (previousValue != 0 ? "PRAGMA foreign_keys=ON" : "PRAGMA foreign_keys=OFF");
    int actual{};
    session << "PRAGMA foreign_keys", soci::into(actual);
    if (actual != previousValue)
        throw MigrationError{ErrorCode::InvalidSchema, "SQLite foreign_keys restoration failed"};
}

namespace detail
{
MigrationState::MigrationState(Database& database, const Catalog& catalog, const SessionOperations& operations)
    : database_{database},
      catalog_{catalog},
      operations_{operations},
      backend_{ready(database)},
      runtime_{Access::runtime(database)},
      session_{Access::session(database)}
{
    validateCatalog(catalog_, backend_);
    try
    {
        table_ = runtime_.migrationTable(session_);
    }
    catch (const soci::soci_error& error)
    {
        databaseFailure(error, runtime_, backend_, 0);
    }
}

auto MigrationState::inspect() const -> Status
{
    return evaluate(catalog_, backend_, readHistory(session_, runtime_, table_),
                    readIncomplete(session_, runtime_, table_));
}

auto MigrationState::status() const -> Status
{
    try
    {
        Transaction transaction{database_, table_, false, operations_};
        auto result = inspect();
        transaction.commit(0);
        return result;
    }
    catch (const soci::soci_error& error)
    {
        databaseFailure(error, runtime_, backend_, 0);
    }
    catch (const MigrationError& error)
    {
        throw MigrationError{error.getCode(), error.what(), error.getVersion(), backend_, error.getNativeCode()};
    }
}

auto MigrationState::plan(const Status& initial, Direction direction, std::optional<Version> target) const -> Plan
{
    auto result = makePlan(catalog_, backend_, initial, direction, target);
    result.atomicity = database_.getBackendCapabilities().migrations.atomicity;
    return result;
}

auto MigrationState::validate() const -> void
{
    requireValid(status(), backend_);
}

auto MigrationState::requireHistory(const std::vector<AppliedMigration>& expected, Version version) const -> void
{
    if (readHistory(session_, runtime_, table_) != expected)
        throw MigrationError{ErrorCode::HistoryChanged, "Migration history changed; inspect and retry", version,
                             backend_};
}

auto MigrationState::requireMarker(const IncompleteMigration& expected, std::string_view message) const -> void
{
    if (readIncomplete(session_, runtime_, table_) != expected)
        throw MigrationError{ErrorCode::InvalidHistory, std::string{message}, expected.version, backend_};
}

auto MigrationState::changeHistory(const Migration& migration, Direction direction) -> void
{
    if (direction == Direction::Up)
        recordHistory(session_, table_, migration, backend_, false);
    else
    {
        const auto version = static_cast<long long>(migration.version);
        session_ << "DELETE FROM " + table_ + " WHERE version=:version", soci::use(version, "version");
    }
}

auto MigrationState::clearMarker(Version version) -> void
{
    session_ << "DELETE FROM " + runtime_.migrationJournalTable(session_, table_) + " WHERE slot=1";
    if (readIncomplete(session_, runtime_, table_))
        throw MigrationError{ErrorCode::InvalidHistory, "Incomplete marker was not removed", version, backend_};
}

auto MigrationState::apply(const Migration& migration, Direction direction,
                           const std::vector<AppliedMigration>& expected, const std::function<void()>& preflight,
                           const std::function<void()>& validateResult) -> std::vector<AppliedMigration>
{
    const auto version = migration.version;
    try
    {
        const auto& scripts = scriptsFor(migration, backend_);
        const auto& sql = direction == Direction::Up ? scripts.up : *scripts.down;
        HistoryWrite write{database_, table_, operations_};
        if (write.nonTransactional())
        {
            requireValid(inspect(), backend_);
            preflight();
            // MySQL-family history DDL must precede the marker transaction.
            runtime_.createMigrationHistory(session_, table_);
            const auto journal = runtime_.migrationJournalTable(session_, table_);
            if (journal.empty())
                throw MigrationError{ErrorCode::UnsupportedBackend,
                                     "Nontransactional migrations require a durable journal", version, backend_};
            {
                Transaction marker{database_, table_, false, operations_};
                requireHistory(expected, version);
                recordIncomplete(session_, journal, migration, backend_, direction);
                requireMarker({version, migration.name, backend_, direction, checksum(scripts)},
                              "Incomplete marker was not recorded");
                marker.commit(version);
            }
            operations_.executeScript(session_, runtime_, sql, version);
        }
        write.beginHistory();
        requireHistory(expected, version);
        if (write.nonTransactional())
            requireMarker({version, migration.name, backend_, direction, checksum(scripts)},
                          "Migration script modified its incomplete marker");
        else
        {
            preflight();
            runtime_.createMigrationHistory(session_, table_);
            operations_.executeScript(session_, runtime_, sql, version);
        }
        validateResult();
        changeHistory(migration, direction);
        auto actual = readHistory(session_, runtime_, table_);
        const auto wantedSize = direction == Direction::Up ? expected.size() + 1 : expected.size() - 1;
        const auto preservedSize = direction == Direction::Up ? expected.size() : wantedSize;
        if (actual.size() != wantedSize ||
            !std::equal(expected.begin(), expected.begin() + static_cast<std::ptrdiff_t>(preservedSize),
                        actual.begin()))
            throw MigrationError{ErrorCode::InvalidHistory, "Migration script modified its history", version, backend_};
        requireValid(evaluate(catalog_, backend_, actual), backend_);
        if (write.nonTransactional())
            clearMarker(version);
        write.commit(version);
        return actual;
    }
    catch (const soci::soci_error& error)
    {
        databaseFailure(error, runtime_, backend_, version);
    }
    catch (const MigrationError& error)
    {
        throw MigrationError{error.getCode(), error.what(), version, backend_, error.getNativeCode()};
    }
}

auto MigrationState::baseline(Version target) -> void
{
    const auto initial = status();
    requireValid(initial, backend_);
    if (!initial.applied.empty() || target <= 0)
        throw MigrationError{ErrorCode::InvalidTarget, "Baseline requires empty history and a positive target", target,
                             backend_};
    const auto baselinePlan = plan(initial, Direction::Up, target);
    try
    {
        HistoryWrite write{database_, table_, operations_};
        if (write.nonTransactional())
        {
            requireValid(inspect(), backend_);
            runtime_.createMigrationHistory(session_, table_);
        }
        write.beginHistory();
        requireHistory(initial.applied, target);
        if (!write.nonTransactional())
            runtime_.createMigrationHistory(session_, table_);
        for (const auto& migration : catalog_.migrations())
        {
            if (migration.version > target)
                break;
            recordHistory(session_, table_, migration, backend_, true);
        }
        const auto recorded = evaluate(catalog_, backend_, readHistory(session_, runtime_, table_));
        requireValid(recorded, backend_);
        if (recorded.applied.size() != baselinePlan.migrations.size() || recorded.currentVersion != target ||
            recorded.baselineVersion != target)
            throw MigrationError{ErrorCode::InvalidHistory, "Baseline history was not recorded as expected", target,
                                 backend_};
        write.commit(target);
    }
    catch (const soci::soci_error& error)
    {
        databaseFailure(error, runtime_, backend_, target);
    }
    catch (const MigrationError& error)
    {
        throw MigrationError{error.getCode(), error.what(), target, backend_, error.getNativeCode()};
    }
}

auto MigrationState::resolve(Version version, std::string_view expectedChecksum, Resolution resolution) -> void
{
    const auto initial = status();
    if (!initial.problems.empty())
    {
        const auto& problem = initial.problems.front();
        throw MigrationError{problem.code, problem.message, problem.version, backend_};
    }
    if (database_.getBackendCapabilities().migrations.atomicity != db::MigrationAtomicity::NonTransactional ||
        !initial.incomplete || initial.incomplete->version != version ||
        initial.incomplete->checksum != expectedChecksum ||
        (resolution != Resolution::Completed && resolution != Resolution::Reverted))
        throw MigrationError{ErrorCode::InvalidResolution,
                             "Resolution must match the incomplete migration and checksum", version, backend_};
    try
    {
        HistoryWrite write{database_, table_, operations_};
        write.beginHistory();
        requireHistory(initial.applied, version);
        if (readIncomplete(session_, runtime_, table_) != initial.incomplete)
            throw MigrationError{ErrorCode::HistoryChanged, "Migration state changed; inspect and retry", version,
                                 backend_};
        if (resolution == Resolution::Completed)
            changeHistory(*std::ranges::find(catalog_.migrations(), version, &Migration::version),
                          initial.incomplete->direction);
        const auto actual = evaluate(catalog_, backend_, readHistory(session_, runtime_, table_));
        requireValid(actual, backend_);
        const auto expectedSize =
            initial.applied.size() +
            (resolution == Resolution::Completed && initial.incomplete->direction == Direction::Up ? 1 : 0) -
            (resolution == Resolution::Completed && initial.incomplete->direction == Direction::Down ? 1 : 0);
        if (actual.applied.size() != expectedSize)
            throw MigrationError{ErrorCode::InvalidHistory, "Resolution history was not recorded", version, backend_};
        clearMarker(version);
        write.commit(version);
    }
    catch (const soci::soci_error& error)
    {
        databaseFailure(error, runtime_, backend_, version);
    }
}
} // namespace detail
} // namespace orm::migrations
