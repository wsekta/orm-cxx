#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "BackendExpectations.hpp"

import orm;

#include "ConnectionConfig.hpp"

struct PackageModel
{
    int id;
    std::string name;
};
struct PackageSummary
{
    std::string name;
};
using PackageSchema = orm::Schema<PackageModel>;

static_assert(not orm::test::config::expectedSqliteBackend.has_value() or
              orm::config::sqliteBackendEnabled == *orm::test::config::expectedSqliteBackend);
static_assert(not orm::test::config::expectedPostgresqlBackend.has_value() or
              orm::config::postgresqlBackendEnabled == *orm::test::config::expectedPostgresqlBackend);
static_assert(not orm::test::config::expectedMysqlBackend.has_value() or
              orm::config::mysqlBackendEnabled == *orm::test::config::expectedMysqlBackend);
static_assert(not orm::test::config::expectedMariadbBackend.has_value() or
              orm::config::mariadbBackendEnabled == *orm::test::config::expectedMariadbBackend);

struct BackendProbe
{
    orm::db::BackendType type;
    bool enabled;
    const char* environment;
};

template <orm::db::ConnectionConfiguration Config>
void databaseSmoke(const BackendProbe& probe, const Config& config)
{
    using namespace orm::query;
    constexpr auto selectPlan = select<PackageModel>().where(col<&PackageModel::id>() == param<int, 0>());
    constexpr auto updatePlan = orm::query::update<PackageModel>()
                                    .set(col<&PackageModel::name>(), param<std::string, 0>())
                                    .where(col<&PackageModel::id>() == param<int, 1>());
    constexpr auto projectionPlan = selectAs<PackageModel, PackageSummary>(as<"name">(col<&PackageModel::name>()))
                                        .where(col<&PackageModel::id>() == param<int, 0>());
    constexpr auto removePlan = remove<PackageModel>().where(col<&PackageModel::id>() == param<int, 0>());
    orm::Database database{config};
    auto context = database.orm<PackageSchema>();
    orm::migrations::Runner migrations{database,
                                       orm::migrations::Catalog{{1,
                                                                 "package_probe",
                                                                 {{probe.type,
                                                                   {"CREATE TABLE package_migration_probe(id INTEGER);",
                                                                    "DROP TABLE package_migration_probe;",
                                                                    {},
                                                                    {},
                                                                    {},
                                                                    {}}}},
                                                                 {},
                                                                 {}}}};
    migrations.up();
    if (migrations.status().currentVersion != 1)
    {
        throw std::runtime_error("Installed migration up failed");
    }
    migrations.down(0);
    if (migrations.status().currentVersion != 0)
    {
        throw std::runtime_error("Installed migration down failed");
    }
    context.createTable<PackageModel>();
    context.insert(PackageModel{1, "first"});
    const auto entries = context.select(selectPlan, 1);
    if (entries.size() != 1 or entries.front().id != 1 or entries.front().name != "first")
    {
        throw std::runtime_error("Installed select failed");
    }
    if (context.update(updatePlan, std::string{"updated"}, 1) != 1)
    {
        throw std::runtime_error("Installed update failed");
    }
    const auto summaries = context.select(projectionPlan, 1);
    if (summaries.size() != 1 or summaries.front().name != "updated")
    {
        throw std::runtime_error("Installed projection failed");
    }
    if (context.remove(removePlan, 1) != 1 or not context.select(selectPlan, 1).empty())
    {
        throw std::runtime_error("Installed delete failed");
    }
    context.deleteTable<PackageModel>();
    database.disconnect();
}

int main()
{
    try
    {
        const orm::Date date{std::chrono::year{2026}, std::chrono::month{10}, std::chrono::day{24}};
        const orm::LocalDateTime local{std::chrono::microseconds{123456}};
        const orm::UtcTimestamp utc{std::chrono::microseconds{123456}};
        const orm::TimeOfDay time{std::chrono::microseconds{123456}};
        const auto uuid = orm::Uuid::parse("00112233-4455-6677-8899-aabbccddeeff");
        if (!date.ok() || local.time_since_epoch() != time.sinceMidnight() ||
            utc.time_since_epoch() != time.sinceMidnight() || orm::Uuid{uuid.bytes()} != uuid ||
            uuid.toString() != "00112233-4455-6677-8899-aabbccddeeff")
        {
            throw std::runtime_error{"Installed value type round trip failed"};
        }

        using namespace orm::query;
        // Compile the public typed API even for a package with no drivers.
        constexpr auto plan = select<PackageModel>().where(col<&PackageModel::id>() == param<int, 0>());
        (void)plan.toDynamic(1);
        static_assert(requires(orm::Database& database) { database.orm<PackageSchema>(); });
        const auto snapshot = orm::migrations::capture(PackageSchema::view);
        const auto generated = orm::migrations::generate(
            1, "package_initial", orm::migrations::compare(orm::migrations::emptySnapshot(), snapshot));
        if (generated.migration.scripts.size() != orm::migrations::generationBackends.size())
        {
            throw std::runtime_error("Installed offline generator is missing a backend variant");
        }
        const orm::db::CommandGeneratorFactory factory;
        const std::array probes{
            BackendProbe{orm::db::BackendType::Sqlite, orm::config::sqliteBackendEnabled,
                         "ORM_CXX_SQLITE_TEST_DATABASE"},
            BackendProbe{orm::db::BackendType::Postgres, orm::config::postgresqlBackendEnabled,
                         "ORM_CXX_POSTGRESQL_TEST_DATABASE"},
            BackendProbe{orm::db::BackendType::Mysql, orm::config::mysqlBackendEnabled, "ORM_CXX_MYSQL_TEST_DATABASE"},
            BackendProbe{orm::db::BackendType::Mariadb, orm::config::mariadbBackendEnabled,
                         "ORM_CXX_MARIADB_TEST_DATABASE"}};
        const auto* required = std::getenv("ORM_CXX_REQUIRE_PACKAGE_DATABASE_TESTS");
        for (const auto& probe : probes)
        {
            const orm::db::BackendProvider* provider = nullptr;
            try
            {
                provider = &factory.getBackend(probe.type);
            }
            catch (const std::out_of_range&)
            {
            }
            if ((provider != nullptr) != probe.enabled or (provider != nullptr and provider->type() != probe.type))
            {
                throw std::runtime_error("Backend registration differs from package features");
            }
            if (not probe.enabled)
            {
                continue;
            }
            const auto* configured = std::getenv(probe.environment);
            if (probe.type == orm::db::BackendType::Sqlite)
                databaseSmoke(probe, orm::SqliteConfig{.database = orm::SqliteMemory{}});
            else if (configured != nullptr or (required != nullptr and std::string{required} == "1"))
            {
                switch (probe.type)
                {
                case orm::db::BackendType::Postgres:
                    databaseSmoke(probe, orm::test::postgresConfigFromEnvironment());
                    break;
                case orm::db::BackendType::Mysql:
                    databaseSmoke(probe, orm::test::mysqlConfigFromEnvironment());
                    break;
                case orm::db::BackendType::Mariadb:
                    databaseSmoke(probe, orm::test::mariadbConfigFromEnvironment());
                    break;
                default:
                    break;
                }
            }
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
