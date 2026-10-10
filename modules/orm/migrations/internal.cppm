module;

#include <memory>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "../../detail/SociTypes.inc"

module orm:migrations_internal;

import :migrations;
import :foundation;
import :sql;
import :database;

namespace orm::migrations::detail
{
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
};

[[nodiscard]] auto normalizeSql(std::string_view sql) -> std::string;
[[nodiscard]] auto sha256(std::string_view bytes) -> std::string;
[[nodiscard]] auto checksum(const SqlMigration& scripts) -> std::string;
auto validateScript(std::string_view script, db::BackendType backend, Version version) -> void;
[[nodiscard]] auto backendName(db::BackendType backend) -> std::string_view;
auto runCommandLineWithConnection(std::span<const std::string_view> args, const Catalog& catalog,
                                  std::optional<std::string_view> connection, std::ostream& output,
                                  std::ostream& errors) -> int;
} // namespace orm::migrations::detail
