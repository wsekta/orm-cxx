module;

#include "tests/UnitTestPrelude.hpp"

module orm;

import :internal;
import :test_support;
import :migrations_internal;
import :value_codec;

namespace migration_adapter_types
{
struct DomainUuid
{
    orm::Uuid value;
};
} // namespace migration_adapter_types

template <>
struct orm::UuidAdapter<migration_adapter_types::DomainUuid>
{
    static auto toUuid(const migration_adapter_types::DomainUuid& value) -> orm::Uuid
    {
        return value.value;
    }
    static auto fromUuid(const orm::Uuid& value) -> migration_adapter_types::DomainUuid
    {
        return {value};
    }
};

namespace temporal_migration_models
{
struct Seed
{
    int id;
    inline static constexpr orm::reflection::FixedString table_name{"migration_temporal_values"};
};
struct Values
{
    int id;
    orm::Date day;
    orm::TimeOfDay time;
    orm::LocalDateTime local;
    orm::UtcTimestamp instant;
    std::optional<orm::UtcTimestamp> optionalInstant;
    inline static constexpr orm::reflection::FixedString table_name{"migration_temporal_values"};
};
struct DateOnly
{
    int id;
    orm::Date value;
    inline static constexpr orm::reflection::FixedString table_name{"migration_temporal_values"};
};
struct TimeOnly
{
    int id;
    orm::TimeOfDay value;
    inline static constexpr orm::reflection::FixedString table_name{"migration_temporal_values"};
};
struct UuidOnly
{
    int id;
    orm::Uuid value;
    inline static constexpr orm::reflection::FixedString table_name{"migration_temporal_values"};
};
struct UuidTarget
{
    orm::Uuid id;
    inline static constexpr orm::reflection::FixedString table_name{"migration_uuid_targets"};
};
struct UuidOwner
{
    orm::Uuid id;
    std::optional<UuidTarget> target;
    orm::ManyToMany<UuidTarget> targets;
    inline static constexpr orm::reflection::FixedString table_name{"migration_uuid_owners"};
    inline static constexpr auto relations = orm::relations(orm::manyToMany<&UuidOwner::targets>()
                                                                .through<"migration_uuid_links">()
                                                                .ownerColumns<"owner_id">()
                                                                .targetColumns<"target_id">());
};
struct BinaryUuidTarget
{
    orm::Uuid id;
    inline static constexpr orm::reflection::FixedString table_name{"migration_binary_targets"};
    inline static constexpr auto uuid_storage = orm::uuidStorages(
        orm::uuidStorage<&BinaryUuidTarget::id, orm::db::BackendType::Mysql, orm::model::UuidStorage::Binary16>(),
        orm::uuidStorage<&BinaryUuidTarget::id, orm::db::BackendType::Sqlite, orm::model::UuidStorage::Binary16>());
};
struct BinaryUuidOwner
{
    orm::Uuid id;
    std::optional<BinaryUuidTarget> target;
    orm::ManyToMany<BinaryUuidTarget> targets;
    inline static constexpr orm::reflection::FixedString table_name{"migration_binary_owners"};
    inline static constexpr auto uuid_storage = orm::uuidStorages(
        orm::uuidStorage<&BinaryUuidOwner::id, orm::db::BackendType::Mysql, orm::model::UuidStorage::Binary16>(),
        orm::uuidStorage<&BinaryUuidOwner::id, orm::db::BackendType::Sqlite, orm::model::UuidStorage::Binary16>());
    inline static constexpr auto relations = orm::relations(orm::manyToMany<&BinaryUuidOwner::targets>()
                                                                .through<"migration_binary_links">()
                                                                .ownerColumns<"owner_id">()
                                                                .targetColumns<"target_id">());
};
struct CanonicalUuidMapping
{
    int id;
    orm::Uuid value;
    std::optional<orm::Uuid> optionalValue;
    inline static constexpr orm::reflection::FixedString table_name{"uuid_mapping_no_diff"};
    inline static constexpr auto uuid_storage =
        orm::uuidStorages(orm::uuidStorage<&CanonicalUuidMapping::value, orm::db::BackendType::Mysql,
                                           orm::model::UuidStorage::Binary16>(),
                          orm::uuidStorage<&CanonicalUuidMapping::value, orm::db::BackendType::Sqlite,
                                           orm::model::UuidStorage::Binary16>(),
                          orm::uuidStorage<&CanonicalUuidMapping::optionalValue, orm::db::BackendType::Mysql,
                                           orm::model::UuidStorage::Binary16>(),
                          orm::uuidStorage<&CanonicalUuidMapping::optionalValue, orm::db::BackendType::Sqlite,
                                           orm::model::UuidStorage::Binary16>());
};
struct AdaptedUuidMapping
{
    int id;
    migration_adapter_types::DomainUuid value;
    std::optional<migration_adapter_types::DomainUuid> optionalValue;
    inline static constexpr orm::reflection::FixedString table_name{"uuid_mapping_no_diff"};
    inline static constexpr auto uuid_storage = orm::uuidStorages(
        orm::uuidStorage<&AdaptedUuidMapping::value, orm::db::BackendType::Mysql, orm::model::UuidStorage::Binary16>(),
        orm::uuidStorage<&AdaptedUuidMapping::value, orm::db::BackendType::Sqlite, orm::model::UuidStorage::Binary16>(),
        orm::uuidStorage<&AdaptedUuidMapping::optionalValue, orm::db::BackendType::Mysql,
                         orm::model::UuidStorage::Binary16>(),
        orm::uuidStorage<&AdaptedUuidMapping::optionalValue, orm::db::BackendType::Sqlite,
                         orm::model::UuidStorage::Binary16>());
};
using SeedSchema = orm::Schema<Seed>;
using ValueSchema = orm::Schema<Values>;
using DateSchema = orm::Schema<DateOnly>;
using TimeSchema = orm::Schema<TimeOnly>;
using UuidValueSchema = orm::Schema<UuidOnly>;
using UuidSchema = orm::Schema<UuidOwner, UuidTarget>;
using BinaryUuidSchema = orm::Schema<BinaryUuidOwner, BinaryUuidTarget>;
using CanonicalUuidMappingSchema = orm::Schema<CanonicalUuidMapping>;
using AdaptedUuidMappingSchema = orm::Schema<AdaptedUuidMapping>;
} // namespace temporal_migration_models

namespace
{
using namespace orm::migrations;
using namespace std::chrono_literals;
using Backend = orm::db::BackendType;
using Type = orm::model::ColumnType;
namespace temporal_detail = orm::migrations::detail;
namespace codec = orm::detail::values;
namespace temporal_models = temporal_migration_models;

class LimitedDateBackend final : public orm::db::BackendProvider
{
public:
    LimitedDateBackend(orm::db::CommandGeneratorFactory registryInit, orm::Date maximumDate)
        : registry{std::move(registryInit)},
          delegate{registry.getBackend(Backend::Postgres)},
          profile{delegate.capabilities()}
    {
        profile.valueLimits.maxDate = maximumDate;
    }
    auto type() const noexcept -> Backend override
    {
        return delegate.type();
    }
    auto capabilities() const noexcept -> const orm::db::BackendCapabilities& override
    {
        return profile;
    }
    auto dialect() const noexcept -> const orm::db::SqlDialect& override
    {
        return delegate.dialect();
    }
    auto runtime() const noexcept -> const orm::db::BackendRuntime& override
    {
        return delegate.runtime();
    }
    auto commandGenerator() const noexcept -> const orm::db::CommandGenerator& override
    {
        return delegate.commandGenerator();
    }
    auto compiledSqlFlavor() const noexcept -> orm::db::CompiledSqlFlavor override
    {
        return delegate.compiledSqlFlavor();
    }

private:
    orm::db::CommandGeneratorFactory registry;
    const orm::db::BackendProvider& delegate;
    orm::db::BackendCapabilities profile;
};

auto temporalColumn(const SchemaSnapshot& snapshot, Backend backend, std::string_view name) -> const PhysicalColumn&
{
    const auto& columns = snapshot.backends.at(backend).tables.front().columns;
    return *std::ranges::find(columns, name, &PhysicalColumn::name);
}

auto temporalApprovals(const SchemaSnapshot& before, const SchemaSnapshot& after,
                       GenerationDecisions decisions = {}) -> GenerationDecisions
{
    for (const auto& change : compare(before, after, decisions).changes)
        if (change.dataLoss)
            decisions.allowDataLossUp.push_back(change.id);
    for (const auto& change : compare(after, before).changes)
        if (change.dataLoss)
            decisions.allowDataLossDown.push_back(change.id);
    return decisions;
}

auto temporalLiteral(Backend backend, Type type, std::string native, GenerationLiteral value) -> GeneratedMigration
{
    const auto before = capture(temporal_models::SeedSchema::view, std::array{backend});
    auto after = before;
    after.backends.at(backend).tables.front().columns.push_back(
        {.name = "value", .logicalType = type, .sqlType = std::move(native), .nullable = false});
    GenerationDecisions decisions;
    decisions.backfills.push_back(
        {.table = "migration_temporal_values", .column = "value", .constant = std::move(value)});
    return generate(2, "temporal_literal", compare(before, after, decisions), decisions);
}

TEST(TemporalMigrationTest, SnapshotRoundTripFreezesNativePrecisionAndTimestampSemantics)
{
    for (const auto backend : {Backend::Postgres, Backend::Mysql, Backend::Mariadb})
    {
        SCOPED_TRACE(static_cast<int>(backend));
        const bool postgres = backend == Backend::Postgres;
        const auto snapshot = capture(temporal_models::ValueSchema::view, std::array{backend});
        EXPECT_EQ(temporalColumn(snapshot, backend, "day").sqlType, "DATE");
        EXPECT_EQ(temporalColumn(snapshot, backend, "time").sqlType,
                  postgres ? "TIME(6) WITHOUT TIME ZONE" : "TIME(6)");
        EXPECT_EQ(temporalColumn(snapshot, backend, "local").sqlType,
                  postgres ? "TIMESTAMP(6) WITHOUT TIME ZONE" : "DATETIME(6)");
        EXPECT_EQ(temporalColumn(snapshot, backend, "instant").sqlType,
                  postgres ? "TIMESTAMP(6) WITH TIME ZONE" : "TIMESTAMP(6)");
        const auto json = temporal_detail::snapshotJson(snapshot);
        EXPECT_EQ(temporal_detail::snapshotFromJson(json), snapshot);
        EXPECT_EQ(temporal_detail::snapshotHash(snapshot, backend),
                  temporal_detail::snapshotHash(temporal_detail::snapshotFromJson(json), backend));
        EXPECT_TRUE(compare(snapshot, temporal_detail::snapshotFromJson(json)).changes.empty());
        const auto generated = generate(1, "native_temporal", compare(emptySnapshot(std::array{backend}), snapshot));
        const auto& scripts = generated.migration.scripts.at(backend);
        EXPECT_EQ(temporal_detail::contextFromJson(temporal_detail::contextJson(scripts.upContext)), scripts.upContext);
        if (!postgres)
            EXPECT_NE(scripts.up.find("`optionalInstant` TIMESTAMP(6) NULL"), std::string::npos);
    }
    EXPECT_THROW((void)capture(temporal_models::ValueSchema::view, std::array{Backend::Sqlite}), GenerationError);
}

TEST(TemporalMigrationTest, SnapshotsRejectPrecisionLossAndNonNativeEmulation)
{
    for (const auto backend : {Backend::Postgres, Backend::Mysql, Backend::Mariadb})
    {
        auto snapshot = capture(temporal_models::ValueSchema::view, std::array{backend});
        auto& columns = snapshot.backends.at(backend).tables.front().columns;
        auto& time = *std::ranges::find(columns, "time", &PhysicalColumn::name);
        const auto canonical = time.sqlType;
        time.sqlType.replace(time.sqlType.find("(6)"), 3, "(0)");
        EXPECT_THROW(temporal_detail::validateSnapshot(snapshot), GenerationError);
        time.sqlType = "TEXT";
        EXPECT_THROW(temporal_detail::validateSnapshot(snapshot), GenerationError);
        time.sqlType = canonical + " DEFAULT NULL";
        EXPECT_THROW(temporal_detail::validateSnapshot(snapshot), GenerationError);
    }
}

TEST(TemporalMigrationTest, UuidKeysForeignKeysAndJunctionsStayNative)
{
    for (const auto backend : {Backend::Postgres, Backend::Mariadb})
    {
        const auto snapshot = capture(temporal_models::UuidSchema::view, std::array{backend});
        EXPECT_EQ(snapshot.backends.at(backend).tables.size(), 3U);
        for (const auto& table : snapshot.backends.at(backend).tables)
        {
            for (const auto& column : table.columns)
            {
                EXPECT_EQ(column.logicalType, Type::Uuid);
                EXPECT_EQ(column.sqlType, "UUID");
            }
        }
        EXPECT_EQ(temporal_detail::snapshotFromJson(temporal_detail::snapshotJson(snapshot)), snapshot);
        EXPECT_NO_THROW((void)generate(1, "uuid_relations", compare(emptySnapshot(std::array{backend}), snapshot)));
    }
    for (const auto backend : {Backend::Sqlite, Backend::Mysql})
        EXPECT_THROW((void)capture(temporal_models::UuidSchema::view, std::array{backend}), GenerationError);
}

TEST(TemporalMigrationTest, BinaryUuidSnapshotsAndBackfillsPreserveCanonicalBytes)
{
    const auto value = orm::Uuid::parse("00112233-4455-6677-8899-aabbccddeeff");
    for (const auto backend : {Backend::Sqlite, Backend::Mysql})
    {
        SCOPED_TRACE(static_cast<int>(backend));
        const auto physicalType = backend == Backend::Sqlite ? "BLOB" : "BINARY(16)";
        auto snapshot = capture(temporal_models::SeedSchema::view, std::array{backend});
        auto& columns = snapshot.backends.at(backend).tables.front().columns;
        columns.push_back({.name = "value", .logicalType = Type::Uuid, .sqlType = physicalType, .nullable = false});
        const auto json = temporal_detail::snapshotJson(snapshot);
        EXPECT_EQ(temporal_detail::snapshotFromJson(json), snapshot);
        EXPECT_TRUE(compare(snapshot, temporal_detail::snapshotFromJson(json)).changes.empty());
        const auto migration = temporalLiteral(backend, Type::Uuid, physicalType, value);
        EXPECT_NE(migration.migration.scripts.at(backend).up.find("X'00112233445566778899aabbccddeeff'"),
                  std::string::npos);
        if (backend == Backend::Mysql)
        {
            const auto& sql = migration.migration.scripts.at(backend).up;
            EXPECT_NE(sql.find("LONGBLOB"), std::string::npos);
            EXPECT_NE(sql.find("CHECK (OCTET_LENGTH("), std::string::npos);
            EXPECT_NE(sql.find("ENFORCED"), std::string::npos);
            EXPECT_NE(sql.find("DROP CHECK"), std::string::npos);
        }
        columns.back().sqlType = backend == Backend::Sqlite ? "TEXT" : "BINARY(15)";
        EXPECT_THROW(temporal_detail::validateSnapshot(snapshot), GenerationError);
    }
}

TEST(TemporalMigrationTest, LegacyNativeUuidSnapshotKeepsCanonicalJsonAndHash)
{
    constexpr std::string_view legacy = R"json({
  "backends": {
    "postgresql": {
      "backend": "postgresql",
      "profile": "orm-cxx-ddl-v1:postgresql",
      "tables": [
        {
          "columns": [
            {
              "autoIncrement": false,
              "logicalType": 12,
              "maxStringLength": null,
              "name": "value",
              "nullable": false,
              "sqlType": "UUID"
            }
          ],
          "foreignKeys": [],
          "indexes": [],
          "name": "legacy_uuid",
          "options": "",
          "primaryKey": [],
          "primaryKeyName": ""
        }
      ]
    }
  },
  "formatVersion": 1
}
)json";
    const auto snapshot = temporal_detail::snapshotFromJson(legacy);
    EXPECT_EQ(temporal_detail::snapshotJson(snapshot), legacy);
    EXPECT_EQ(temporal_detail::snapshotHash(snapshot, Backend::Postgres),
              "87e8b84c9ecc87a09fcaaf3aefed5b724bc8ac17cde21c0ebc85de84d07234da");
    const auto physical = snapshot.backends.at(Backend::Postgres);
    SqlMigration scripts;
    scripts.up = "SELECT 1;\r\n";
    scripts.down = "SELECT 2;\n";
    scripts.upContext.source = physical;
    scripts.upContext.target = physical;
    scripts.downContext.source = physical;
    scripts.downContext.target = physical;
    scripts.beforeHash = temporal_detail::snapshotHash(snapshot, Backend::Postgres);
    scripts.afterHash = scripts.beforeHash;
    const Catalog catalog{Migration{.version = 1,
                                    .name = "legacy_native_uuid",
                                    .scripts = {{Backend::Postgres, scripts}},
                                    .before = snapshot,
                                    .after = snapshot}};
    EXPECT_EQ(temporal_detail::checksum(catalog.migrations().front().scripts.at(Backend::Postgres)),
              "6022cb4503728c560758ca79f68dde2f4bbb2d33091dbf6f55a114cf1aa4e1d1");
}

TEST(TemporalMigrationTest, BinaryUuidKeyMappingsPropagateToForeignAndJunctionColumns)
{
    orm::db::CommandGeneratorFactory registry;
    for (const auto backend : {Backend::Sqlite, Backend::Mysql, Backend::Postgres, Backend::Mariadb})
    {
        const auto snapshot = capture(temporal_models::BinaryUuidSchema::view, std::array{backend});
        const auto expected = backend == Backend::Sqlite ? "BLOB" : backend == Backend::Mysql ? "BINARY(16)" : "UUID";
        ASSERT_EQ(snapshot.backends.at(backend).tables.size(), 3U);
        for (const auto& table : snapshot.backends.at(backend).tables)
            for (const auto& column : table.columns)
            {
                EXPECT_EQ(column.logicalType, Type::Uuid);
                EXPECT_EQ(column.sqlType, expected);
            }
        EXPECT_EQ(temporal_detail::snapshotFromJson(temporal_detail::snapshotJson(snapshot)), snapshot);
        const bool enabled = backend == Backend::Sqlite   ? orm::config::sqliteBackendEnabled :
                             backend == Backend::Mysql    ? orm::config::mysqlBackendEnabled :
                             backend == Backend::Postgres ? orm::config::postgresqlBackendEnabled :
                                                            orm::config::mariadbBackendEnabled;
        if (!enabled)
            continue;
        const auto owner = temporal_models::BinaryUuidSchema::view.at(0);
        const auto& provider = registry.getBackend(backend);
        EXPECT_NE(provider.commandGenerator().createTable(owner).find(expected), std::string::npos);
        const auto junctions = orm::db::relations::createTableStatements(provider.dialect(), owner, backend);
        ASSERT_EQ(junctions.size(), 1U);
        EXPECT_NE(junctions.front().find(expected), std::string::npos);
        EXPECT_THROW((void)orm::db::relations::createTableStatements(provider.dialect(), owner), GenerationError);
    }
}

TEST(TemporalMigrationTest, AdaptingTheCppUuidTypeDoesNotChangeThePhysicalSchema)
{
    for (const auto backend : {Backend::Sqlite, Backend::Mysql, Backend::Postgres, Backend::Mariadb})
    {
        const auto canonical = capture(temporal_models::CanonicalUuidMappingSchema::view, std::array{backend});
        const auto adapted = capture(temporal_models::AdaptedUuidMappingSchema::view, std::array{backend});
        EXPECT_EQ(canonical, adapted);
        EXPECT_TRUE(compare(canonical, adapted).changes.empty());
        EXPECT_EQ(temporal_detail::snapshotHash(canonical, backend), temporal_detail::snapshotHash(adapted, backend));
    }
}

TEST(TemporalMigrationTest, TypedLiteralsRetainMicrosecondsAndUseNativeCasts)
{
    const auto day = codec::parseDate("2024-02-29");
    const auto time = orm::TimeOfDay{12h + 34min + 56s + 123'456us};
    const auto local = codec::parseLocalDateTime("2024-02-29 12:34:56.123456");
    const auto instant = codec::parseUtcTimestamp("2024-02-29 12:34:56.000001+00:00");
    for (const auto backend : {Backend::Postgres, Backend::Mysql, Backend::Mariadb})
    {
        const bool postgres = backend == Backend::Postgres;
        const auto dateSql = temporalLiteral(backend, Type::Date, "DATE", day).migration.scripts.at(backend).up;
        EXPECT_NE(dateSql.find("'2024-02-29'"), std::string::npos);
        const auto timeSql =
            temporalLiteral(backend, Type::TimeOfDay, postgres ? "TIME(6) WITHOUT TIME ZONE" : "TIME(6)", time)
                .migration.scripts.at(backend)
                .up;
        EXPECT_NE(timeSql.find("'12:34:56.123456'"), std::string::npos);
        const auto localSql = temporalLiteral(backend, Type::LocalDateTime,
                                              postgres ? "TIMESTAMP(6) WITHOUT TIME ZONE" : "DATETIME(6)", local)
                                  .migration.scripts.at(backend)
                                  .up;
        EXPECT_NE(localSql.find("'2024-02-29 12:34:56.123456'"), std::string::npos);
        const auto utcSql = temporalLiteral(backend, Type::UtcTimestamp,
                                            postgres ? "TIMESTAMP(6) WITH TIME ZONE" : "TIMESTAMP(6)", instant)
                                .migration.scripts.at(backend)
                                .up;
        EXPECT_NE(utcSql.find(postgres ? "'2024-02-29 12:34:56.000001+00:00'" : "'2024-02-29 12:34:56.000001'"),
                  std::string::npos);
        if (postgres)
            EXPECT_NE(utcSql.find("AS TIMESTAMP(6) WITH TIME ZONE"), std::string::npos);
    }
}

TEST(TemporalMigrationTest, LiteralValidationChecksCalendarNativeRangesAndLogicalIdentity)
{
    const auto invalidDay = orm::Date{std::chrono::year{2023}, std::chrono::February, std::chrono::day{29}};
    EXPECT_THROW((void)temporalLiteral(Backend::Postgres, Type::Date, "DATE", invalidDay), GenerationError);
    const auto earlyDay = codec::parseDate("0999-12-31");
    EXPECT_NO_THROW((void)temporalLiteral(Backend::Postgres, Type::Date, "DATE", earlyDay));
    for (const auto backend : {Backend::Mysql, Backend::Mariadb})
        EXPECT_THROW((void)temporalLiteral(backend, Type::Date, "DATE", earlyDay), GenerationError);
    EXPECT_THROW((void)temporalLiteral(Backend::Postgres, Type::Date, "DATE", orm::TimeOfDay{}), GenerationError);
    EXPECT_THROW((void)temporalLiteral(Backend::Postgres, Type::Date, "DATE", std::string{"2024-02-29"}),
                 GenerationError);
    const auto tooEarly = codec::parseUtcTimestamp("1970-01-01 00:00:00.999999+00:00");
    for (const auto backend : {Backend::Mysql, Backend::Mariadb})
        EXPECT_THROW((void)temporalLiteral(backend, Type::UtcTimestamp, "TIMESTAMP(6)", tooEarly), GenerationError);
    const auto afterMysql = codec::parseUtcTimestamp("2038-01-19 03:14:07.500000+00:00");
    EXPECT_THROW((void)temporalLiteral(Backend::Mysql, Type::UtcTimestamp, "TIMESTAMP(6)", afterMysql),
                 GenerationError);
}

TEST(TemporalMigrationTest, DecisionsJsonKeepsTemporalAndUuidValuesTyped)
{
    const auto decisions = temporal_detail::decisionsFromJson(R"json({"backfills":[
        {"table":"records","column":"d","constant":{"type":"date","value":"2024-02-29"}},
        {"table":"records","column":"t","constant":{"type":"time_of_day","value":"00:00:00.000001"}},
        {"table":"records","column":"l","constant":{"type":"local_date_time","value":"2024-02-29 12:34:56.123456"}},
        {"table":"records","column":"u","constant":{"type":"utc_timestamp","value":"2024-02-29 12:34:56.123456+00:00"}},
        {"table":"records","column":"id","constant":{"type":"uuid","value":"01234567-89ab-7def-8123-456789abcdef"}}
    ]})json");
    ASSERT_EQ(decisions.backfills.size(), 5U);
    EXPECT_EQ(std::get<orm::Date>(*decisions.backfills[0].constant), codec::parseDate("2024-02-29"));
    EXPECT_EQ(std::get<orm::TimeOfDay>(*decisions.backfills[1].constant).sinceMidnight(), 1us);
    EXPECT_EQ(std::get<orm::LocalDateTime>(*decisions.backfills[2].constant),
              codec::parseLocalDateTime("2024-02-29 12:34:56.123456"));
    EXPECT_EQ(std::get<orm::UtcTimestamp>(*decisions.backfills[3].constant),
              codec::parseUtcTimestamp("2024-02-29 12:34:56.123456+00:00"));
    EXPECT_EQ(std::get<orm::Uuid>(*decisions.backfills[4].constant).toString(), "01234567-89ab-7def-8123-456789abcdef");
    for (const auto typed :
         {R"json({"type":"date","value":"2023-02-29"})json", R"json({"type":"time_of_day","value":"24:00:00"})json",
          R"json({"type":"uuid","value":"malformed"})json", R"json({"type":"unknown","value":"2024-02-29"})json",
          R"json({"type":"date","value":"2024-02-29","extra":1})json"})
    {
        EXPECT_THROW(
            (void)temporal_detail::decisionsFromJson(
                std::string{"{\"backfills\":[{\"table\":\"r\",\"column\":\"v\",\"constant\":"} + typed + "}]}"),
            MigrationError);
    }
}

TEST(TemporalMigrationTest, LocalAndUtcConversionsRequireAnExplicitBackendExpression)
{
    for (const auto backend : {Backend::Postgres, Backend::Mysql, Backend::Mariadb})
    {
        auto before = capture(temporal_models::SeedSchema::view, std::array{backend});
        before.backends.at(backend).tables.front().columns.push_back(
            {.name = "value",
             .logicalType = Type::LocalDateTime,
             .sqlType = backend == Backend::Postgres ? "TIMESTAMP(6) WITHOUT TIME ZONE" : "DATETIME(6)",
             .nullable = false});
        auto after = before;
        auto& target = after.backends.at(backend).tables.front().columns.back();
        target.logicalType = Type::UtcTimestamp;
        target.sqlType = backend == Backend::Postgres ? "TIMESTAMP(6) WITH TIME ZONE" : "TIMESTAMP(6)";
        const auto pending = compare(before, after);
        ASSERT_EQ(pending.changes.size(), 1U);
        EXPECT_TRUE(pending.changes.front().dataLoss);
        EXPECT_TRUE(pending.changes.front().unresolved);
        GenerationDecisions decisions;
        decisions.allowDataLossUp.push_back(pending.changes.front().id);
        decisions.conversions.push_back(
            {.table = "migration_temporal_values", .column = "value", .sourceColumn = "value"});
        EXPECT_TRUE(compare(before, after, decisions).changes.front().unresolved);
        EXPECT_THROW((void)generate(2, "implicit_timezone", compare(before, after, decisions), decisions),
                     GenerationError);
        decisions.conversions.front().sourceColumn.reset();
        decisions.conversions.front().constant = codec::parseUtcTimestamp("2024-01-01 00:00:00+00:00");
        EXPECT_TRUE(compare(before, after, decisions).changes.front().unresolved);
        decisions.conversions.front().constant.reset();
        decisions.conversions.front().expressions.emplace(backend, backend == Backend::Postgres ?
                                                                       "\"value\" AT TIME ZONE 'UTC'" :
                                                                       "CONVERT_TZ(`value`, '+00:00', '+00:00')");
        EXPECT_FALSE(compare(before, after, decisions).changes.front().unresolved);
        EXPECT_NO_THROW((void)generate(2, "explicit_timezone", compare(before, after, decisions), decisions));
    }
}

TEST(TemporalMigrationTest, BinaryUuidConversionRequiresAnExplicitBackendExpression)
{
    for (const auto backend : {Backend::Sqlite, Backend::Mysql})
    {
        auto before = capture(temporal_models::SeedSchema::view, std::array{backend});
        before.backends.at(backend).tables.front().columns.push_back(
            {.name = "value",
             .logicalType = Type::String,
             .sqlType = backend == Backend::Sqlite ? "TEXT" : "LONGTEXT",
             .nullable = false});
        auto after = before;
        auto& target = after.backends.at(backend).tables.front().columns.back();
        target.logicalType = Type::Uuid;
        target.sqlType = backend == Backend::Sqlite ? "BLOB" : "BINARY(16)";
        auto pending = compare(before, after);
        ASSERT_EQ(pending.changes.size(), 1U);
        EXPECT_TRUE(pending.changes.front().unresolved);
        GenerationDecisions decisions;
        decisions.allowDataLossUp.push_back(pending.changes.front().id);
        decisions.conversions.push_back(
            {.table = "migration_temporal_values", .column = "value", .sourceColumn = "value"});
        EXPECT_TRUE(compare(before, after, decisions).changes.front().unresolved);
        decisions.conversions.front().sourceColumn.reset();
        decisions.conversions.front().constant = orm::Uuid{};
        EXPECT_TRUE(compare(before, after, decisions).changes.front().unresolved);
        decisions.conversions.front().constant.reset();
        decisions.conversions.front().expressions.emplace(backend, "X'00112233445566778899aabbccddeeff'");
        EXPECT_FALSE(compare(before, after, decisions).changes.front().unresolved);
    }
}
} // namespace

class TemporalMigrationLiveTest : public DatabaseTest<temporal_models::SeedSchema>
{
public:
    auto session() -> soci::session&
    {
        return temporal_detail::DatabaseAccess::session(connection);
    }
    auto quote(std::string_view name) const -> std::string
    {
        return temporal_detail::quoteIdentifier(GetParam().type, name);
    }
    auto valuesSnapshot() const -> SchemaSnapshot
    {
        return capture(temporal_models::ValueSchema::view, std::array{GetParam().type});
    }
    auto initial(const SchemaSnapshot& snapshot) -> Migration
    {
        auto migration =
            generate(1, "temporal_initial", compare(emptySnapshot(std::array{GetParam().type}), snapshot)).migration;
        Runner{connection, Catalog{migration}}.up();
        return migration;
    }
    auto expectDriftRejected(const Catalog& catalog) -> void
    {
        Runner runner{connection, catalog};
        try
        {
            runner.up();
            FAIL() << "A temporal source definition must match its frozen precision and time-zone semantics";
        }
        catch (const MigrationError& error)
        {
            EXPECT_EQ(error.getCode(), ErrorCode::InvalidSchema) << error.what();
            EXPECT_EQ(error.getVersion(), 2);
        }
        EXPECT_EQ(runner.status().currentVersion, 1);
        EXPECT_FALSE(runner.status().incomplete);
        int added{};
        session() << "SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=" +
                         std::string{GetParam().type == Backend::Postgres ? "current_schema()" : "DATABASE()"} +
                         " AND table_name='migration_temporal_values' AND column_name='extra'",
            soci::into(added);
        EXPECT_EQ(added, 0);
    }
};

TEST_P(TemporalMigrationLiveTest, TypedBackfillsRoundTripMicrosecondsAndNullableUtc)
{
    if (GetParam().type == Backend::Sqlite)
        GTEST_SKIP() << "SQLite has no native temporal columns";
    const auto before = capture(temporal_models::SeedSchema::view, std::array{GetParam().type});
    const auto after = valuesSnapshot();
    const auto first = initial(before);
    session() << "INSERT INTO " + quote("migration_temporal_values") + " VALUES(1),(2)";
    GenerationDecisions decisions;
    const auto add = [&](std::string column, GenerationLiteral value)
    {
        decisions.backfills.push_back(
            {.table = "migration_temporal_values", .column = std::move(column), .constant = std::move(value)});
    };
    add("day", codec::parseDate("2024-02-29"));
    add("time", orm::TimeOfDay{12h + 34min + 56s + 123'456us});
    add("local", codec::parseLocalDateTime("2024-02-29 12:34:56.123456"));
    add("instant", codec::parseUtcTimestamp("2024-02-29 12:34:56.000001+00:00"));
    decisions = temporalApprovals(before, after, std::move(decisions));
    const auto second = generate(2, "native_backfills", compare(before, after, decisions), decisions).migration;
    Runner runner{connection, Catalog{first, second}};
    ASSERT_NO_THROW(runner.up());
    auto context = connection.orm<temporal_models::ValueSchema>();
    orm::Query<temporal_models::Values> query;
    const auto rows = context.select(query);
    ASSERT_EQ(rows.size(), 2U);
    for (const auto& row : rows)
    {
        EXPECT_EQ(row.day, codec::parseDate("2024-02-29"));
        EXPECT_EQ(row.time.sinceMidnight(), 12h + 34min + 56s + 123'456us);
        EXPECT_EQ(row.local, codec::parseLocalDateTime("2024-02-29 12:34:56.123456"));
        EXPECT_EQ(row.instant, codec::parseUtcTimestamp("2024-02-29 12:34:56.000001+00:00"));
        EXPECT_FALSE(row.optionalInstant);
    }
    EXPECT_EQ(runner.status().currentVersion, 2);
    ASSERT_NO_THROW(runner.down(1));
    EXPECT_EQ(runner.status().currentVersion, 1);
}

TEST_P(TemporalMigrationLiveTest, ReducedFractionalPrecisionFailsBeforeApplyingDdl)
{
    if (GetParam().type == Backend::Sqlite)
        GTEST_SKIP() << "SQLite has no native temporal columns";
    const auto before = valuesSnapshot();
    const auto first = initial(before);
    auto after = before;
    after.backends.at(GetParam().type)
        .tables.front()
        .columns.push_back({.name = "extra",
                            .logicalType = Type::Int,
                            .sqlType = GetParam().type == Backend::Postgres ? "INTEGER" : "INT",
                            .nullable = true});
    const auto second = generate(2, "extra", compare(before, after)).migration;
    session() << "ALTER TABLE " + quote("migration_temporal_values") +
                     (GetParam().type == Backend::Postgres ?
                          " ALTER COLUMN " + quote("time") + " TYPE TIME(0) WITHOUT TIME ZONE" :
                          " MODIFY COLUMN " + quote("time") + " TIME(0) NOT NULL");
    expectDriftRejected(Catalog{first, second});
}

TEST_P(TemporalMigrationLiveTest, ChangedLocalTimestampSemanticsFailBeforeApplyingDdl)
{
    if (GetParam().type == Backend::Sqlite)
        GTEST_SKIP() << "SQLite has no native temporal columns";
    const auto before = valuesSnapshot();
    const auto first = initial(before);
    auto after = before;
    after.backends.at(GetParam().type)
        .tables.front()
        .columns.push_back({.name = "extra",
                            .logicalType = Type::Int,
                            .sqlType = GetParam().type == Backend::Postgres ? "INTEGER" : "INT",
                            .nullable = true});
    const auto second = generate(2, "extra", compare(before, after)).migration;
    session() << "ALTER TABLE " + quote("migration_temporal_values") +
                     (GetParam().type == Backend::Postgres ?
                          " ALTER COLUMN " + quote("local") + " TYPE TIMESTAMP(6) WITH TIME ZONE" :
                          " MODIFY COLUMN " + quote("local") + " TIMESTAMP(6) NOT NULL");
    expectDriftRejected(Catalog{first, second});
}

TEST_P(TemporalMigrationLiveTest, NativeUuidBackfillsRemainHydratable)
{
    if (GetParam().type != Backend::Postgres && GetParam().type != Backend::Mariadb)
        GTEST_SKIP() << "Native UUID columns require PostgreSQL or MariaDB";
    const auto before = capture(temporal_models::SeedSchema::view, std::array{GetParam().type});
    const auto after = capture(temporal_models::UuidValueSchema::view, std::array{GetParam().type});
    const auto first = initial(before);
    session() << "INSERT INTO " + quote("migration_temporal_values") + " VALUES(1),(2)";
    const auto uuid = orm::Uuid::parse("01234567-89ab-7def-8123-456789abcdef");
    GenerationDecisions decisions;
    decisions.backfills.push_back(
        {.table = "migration_temporal_values", .column = "value", .constant = GenerationLiteral{uuid}});
    decisions = temporalApprovals(before, after, std::move(decisions));
    const auto second = generate(2, "uuid_backfill", compare(before, after, decisions), decisions).migration;
    Runner runner{connection, Catalog{first, second}};
    ASSERT_NO_THROW(runner.up());
    auto context = connection.orm<temporal_models::UuidValueSchema>();
    orm::Query<temporal_models::UuidOnly> query;
    const auto rows = context.select(query);
    ASSERT_EQ(rows.size(), 2U);
    for (const auto& row : rows)
        EXPECT_EQ(row.value, uuid);
    ASSERT_NO_THROW(runner.down(1));
    EXPECT_EQ(runner.status().currentVersion, 1);
}

TEST_P(TemporalMigrationLiveTest, BinaryUuidBackfillRoundTripsAndRollsBack)
{
    if (GetParam().type != Backend::Sqlite && GetParam().type != Backend::Mysql)
        GTEST_SKIP() << "Binary UUID columns require SQLite or MySQL";
    const auto before = capture(temporal_models::SeedSchema::view, std::array{GetParam().type});
    auto after = before;
    after.backends.at(GetParam().type)
        .tables.front()
        .columns.push_back({.name = "value",
                            .logicalType = Type::Uuid,
                            .sqlType = GetParam().type == Backend::Sqlite ? "BLOB" : "BINARY(16)",
                            .nullable = false});
    const auto first = initial(before);
    session() << "INSERT INTO " + quote("migration_temporal_values") + " VALUES(1)";
    GenerationDecisions decisions;
    decisions.backfills.push_back({.table = "migration_temporal_values",
                                   .column = "value",
                                   .constant = orm::Uuid::parse("00112233-4455-6677-8899-aabbccddeeff")});
    decisions = temporalApprovals(before, after, std::move(decisions));
    const auto second = generate(2, "binary_uuid_backfill", compare(before, after, decisions), decisions).migration;
    Runner runner{connection, Catalog{first, second}};
    ASSERT_NO_THROW(runner.up());
    std::string hex;
    session() << "SELECT HEX(" + quote("value") + ") FROM " + quote("migration_temporal_values"), soci::into(hex);
    EXPECT_EQ(hex, "00112233445566778899AABBCCDDEEFF");
    ASSERT_NO_THROW(runner.down(1));
    EXPECT_EQ(runner.status().currentVersion, 1);
}

TEST_P(TemporalMigrationLiveTest, SqliteRejectsInvalidBinaryUuidBackfills)
{
    if (GetParam().type != Backend::Sqlite)
        GTEST_SKIP() << "SQLite storage classes need explicit validation";
    const auto before = capture(temporal_models::SeedSchema::view, std::array{Backend::Sqlite});
    auto after = before;
    after.backends.at(Backend::Sqlite)
        .tables.front()
        .columns.push_back({.name = "value", .logicalType = Type::Uuid, .sqlType = "BLOB", .nullable = false});
    const auto first = initial(before);
    session() << "INSERT INTO " + quote("migration_temporal_values") + " VALUES(1)";
    for (const auto* invalid : {"'0123456789abcdef'", "X'0011'"})
    {
        GenerationDecisions decisions;
        decisions.backfills.push_back(
            {.table = "migration_temporal_values", .column = "value", .expressions = {{Backend::Sqlite, invalid}}});
        decisions = temporalApprovals(before, after, std::move(decisions));
        const auto second = generate(2, "invalid_binary_uuid", compare(before, after, decisions), decisions).migration;
        Runner runner{connection, Catalog{first, second}};
        EXPECT_THROW(runner.up(), MigrationError);
        EXPECT_EQ(runner.status().currentVersion, 1);
        EXPECT_FALSE(runner.status().incomplete);
    }
}

TEST_P(TemporalMigrationLiveTest, CompositeUuidConversionPreservesForeignKeysAndRollsBack)
{
    const auto backend = GetParam().type;
    if (backend != Backend::Sqlite && backend != Backend::Mysql)
        GTEST_SKIP() << "Binary UUID columns require SQLite or MySQL";
    auto before = emptySnapshot(std::array{backend});
    auto& schema = before.backends.at(backend);
    const auto options =
        capture(temporal_models::SeedSchema::view, std::array{backend}).backends.at(backend).tables.front().options;
    const auto textType = backend == Backend::Sqlite ? "TEXT" : "VARCHAR(16)";
    PhysicalTable parent;
    parent.name = "uuid_composite_parent";
    parent.options = options;
    parent.columns = {
        {.name = "part_a", .logicalType = Type::String, .sqlType = textType, .nullable = false, .maxStringLength = 16},
        {.name = "part_b", .logicalType = Type::String, .sqlType = textType, .nullable = false, .maxStringLength = 16}};
    parent.primaryKey = {"part_a", "part_b"};
    parent.primaryKeyName = backend == Backend::Mysql ? "PRIMARY" : "uuid_composite_parent_pk";
    PhysicalTable child;
    child.name = "uuid_composite_child";
    child.options = options;
    child.columns = {
        {.name = "id",
         .logicalType = Type::Int,
         .sqlType = backend == Backend::Sqlite ? "INTEGER" : "INT",
         .nullable = false},
        {.name = "ref_a", .logicalType = Type::String, .sqlType = textType, .nullable = false, .maxStringLength = 16},
        {.name = "ref_b", .logicalType = Type::String, .sqlType = textType, .nullable = false, .maxStringLength = 16}};
    child.primaryKey = {"id"};
    child.primaryKeyName = backend == Backend::Mysql ? "PRIMARY" : "uuid_composite_child_pk";
    child.foreignKeys.push_back({.name = "uuid_composite_fk",
                                 .columns = {"ref_a", "ref_b"},
                                 .targetTable = parent.name,
                                 .targetColumns = {"part_a", "part_b"}});
    if (backend == Backend::Mysql)
        child.indexes.push_back({.name = "uuid_composite_fk_index", .columns = {"ref_a", "ref_b"}});
    schema.tables = {parent, child};
    temporal_detail::canonicalize(before);

    auto after = before;
    for (auto& table : after.backends.at(backend).tables)
        for (auto& column : table.columns)
            if (column.logicalType == Type::String)
            {
                column.logicalType = Type::Uuid;
                column.sqlType = backend == Backend::Sqlite ? "BLOB" : "BINARY(16)";
                column.maxStringLength.reset();
            }
    GenerationDecisions decisions;
    for (const auto& [table, column] :
         std::array{std::pair{"uuid_composite_parent", "part_a"}, std::pair{"uuid_composite_parent", "part_b"},
                    std::pair{"uuid_composite_child", "ref_a"}, std::pair{"uuid_composite_child", "ref_b"}})
    {
        const auto name = quote(column);
        const auto up = "CAST(" + name + (backend == Backend::Sqlite ? " AS BLOB)" : " AS BINARY(16))");
        const auto down =
            backend == Backend::Sqlite ? "CAST(" + name + " AS TEXT)" : "CONVERT(" + name + " USING utf8mb4)";
        decisions.conversions.push_back({.table = table, .column = column, .expressions = {{backend, up}}});
        decisions.downConversions.push_back({.table = table, .column = column, .expressions = {{backend, down}}});
    }
    decisions = temporalApprovals(before, after, std::move(decisions));
    auto missing = decisions;
    missing.conversions.pop_back();
    EXPECT_TRUE(std::ranges::any_of(compare(before, after, missing).changes,
                                    [](const auto& change) { return change.unresolved; }));
    const auto first = initial(before);
    session() << "INSERT INTO " + quote(parent.name) + " (" + quote("part_a") + "," + quote("part_b") +
                     ") VALUES ('0123456789ABCDEF','fedcba9876543210')";
    session() << "INSERT INTO " + quote(child.name) + " (" + quote("id") + "," + quote("ref_a") + "," + quote("ref_b") +
                     ") VALUES (1,'0123456789ABCDEF','fedcba9876543210')";
    const auto second = generate(2, "binary_composite_fk", compare(before, after, decisions), decisions).migration;
    ASSERT_TRUE(second.scripts.at(backend).down);
    Runner runner{connection, Catalog{first, second}};
    ASSERT_NO_THROW(runner.up());
    const auto join = " FROM " + quote(child.name) + " c JOIN " + quote(parent.name) + " p ON c." + quote("ref_a") +
                      " = p." + quote("part_a") + " AND c." + quote("ref_b") + " = p." + quote("part_b");
    int joined{};
    std::string hex;
    session() << "SELECT COUNT(*)" + join, soci::into(joined);
    session() << "SELECT HEX(p." + quote("part_a") + ")" + join, soci::into(hex);
    EXPECT_EQ(joined, 1);
    EXPECT_EQ(hex, "30313233343536373839414243444546");
    ASSERT_NO_THROW(runner.down(1));
    std::string restored;
    joined = 0;
    session() << "SELECT COUNT(*)" + join, soci::into(joined);
    session() << "SELECT c." + quote("ref_a") + join, soci::into(restored);
    EXPECT_EQ(joined, 1);
    EXPECT_EQ(restored, "0123456789ABCDEF");
    EXPECT_EQ(runner.status().currentVersion, 1);
}

TEST_P(TemporalMigrationLiveTest, MysqlRejectsBinaryUuidLengthDrift)
{
    if (GetParam().type != Backend::Mysql)
        GTEST_SKIP() << "MySQL owns the fixed-width binary UUID representation";
    auto before = capture(temporal_models::SeedSchema::view, std::array{Backend::Mysql});
    before.backends.at(Backend::Mysql)
        .tables.front()
        .columns.push_back({.name = "value", .logicalType = Type::Uuid, .sqlType = "BINARY(16)", .nullable = true});
    const auto first = initial(before);
    auto after = before;
    after.backends.at(Backend::Mysql)
        .tables.front()
        .columns.push_back({.name = "extra", .logicalType = Type::Int, .sqlType = "INT", .nullable = true});
    const auto second = generate(2, "binary_uuid_drift", compare(before, after)).migration;
    session() << "ALTER TABLE " + quote("migration_temporal_values") + " MODIFY COLUMN " + quote("value") +
                     " BINARY(15) NULL";
    expectDriftRejected(Catalog{first, second});
}

TEST_P(TemporalMigrationLiveTest, MysqlRejectsShortUuidBackfillBeforePadding)
{
    if (GetParam().type != Backend::Mysql)
        GTEST_SKIP() << "MySQL pads fixed-width binary values";
    const auto before = capture(temporal_models::SeedSchema::view, std::array{Backend::Mysql});
    auto after = before;
    after.backends.at(Backend::Mysql)
        .tables.front()
        .columns.push_back({.name = "value", .logicalType = Type::Uuid, .sqlType = "BINARY(16)", .nullable = false});
    const auto first = initial(before);
    session() << "INSERT INTO " + quote("migration_temporal_values") + " VALUES(1)";
    GenerationDecisions decisions;
    decisions.backfills.push_back(
        {.table = "migration_temporal_values", .column = "value", .expressions = {{Backend::Mysql, "X'01'"}}});
    decisions = temporalApprovals(before, after, std::move(decisions));
    const auto second = generate(2, "short_binary_uuid", compare(before, after, decisions), decisions).migration;
    Runner runner{connection, Catalog{first, second}};
    EXPECT_THROW(runner.up(), MigrationError);
    const auto status = runner.status();
    EXPECT_EQ(status.currentVersion, 1);
    ASSERT_TRUE(status.incomplete);
    EXPECT_EQ(status.incomplete->version, 2);
    int finalColumns{};
    session() << "SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=DATABASE() "
                 "AND table_name='migration_temporal_values' AND column_name='value'",
        soci::into(finalColumns);
    EXPECT_EQ(finalColumns, 0);
}

TEST_P(TemporalMigrationLiveTest, GeneratedUuidPrimaryForeignAndJunctionKeysUseNativeColumns)
{
    if (GetParam().type != Backend::Postgres && GetParam().type != Backend::Mariadb)
        GTEST_SKIP() << "Native UUID columns require PostgreSQL or MariaDB";
    const auto schema = capture(temporal_models::UuidSchema::view, std::array{GetParam().type});
    const auto first = initial(schema);
    const auto target = std::string{"01234567-89ab-7def-8123-456789abcdef"};
    const auto owner = std::string{"fedcba98-7654-4321-8123-456789abcdef"};
    session() << "INSERT INTO " + quote("migration_uuid_targets") + " VALUES('" + target + "')";
    session() << "INSERT INTO " + quote("migration_uuid_owners") + " (" + quote("id") + "," + quote("target_id") +
                     ") VALUES('" + owner + "','" + target + "')";
    session() << "INSERT INTO " + quote("migration_uuid_links") + " (" + quote("owner_id") + "," + quote("target_id") +
                     ") VALUES('" + owner + "','" + target + "')";
    int links{};
    session() << "SELECT COUNT(*) FROM " + quote("migration_uuid_links"), soci::into(links);
    EXPECT_EQ(links, 1);
    auto context = connection.orm<temporal_models::UuidSchema>();
    orm::Query<temporal_models::UuidTarget> query;
    const auto targets = context.select(query);
    ASSERT_EQ(targets.size(), 1U);
    EXPECT_EQ(targets.front().id, orm::Uuid::parse(target));
    EXPECT_EQ((Runner{connection, Catalog{first}}.status().currentVersion), 1);
}

TEST_P(TemporalMigrationLiveTest, PostgresqlInfinityCannotBecomeAModelDate)
{
    if (GetParam().type != Backend::Postgres)
        GTEST_SKIP() << "PostgreSQL infinity is outside the public date domain";
    const auto before = capture(temporal_models::SeedSchema::view, std::array{GetParam().type});
    const auto after = capture(temporal_models::DateSchema::view, std::array{GetParam().type});
    const auto first = initial(before);
    session() << "INSERT INTO " + quote("migration_temporal_values") + " VALUES(1)";
    GenerationDecisions decisions;
    decisions.backfills.push_back({.table = "migration_temporal_values",
                                   .column = "value",
                                   .expressions = {{Backend::Postgres, "'infinity'::date"}}});
    const auto second = generate(2, "invalid_date_domain", compare(before, after, decisions), decisions).migration;
    Runner runner{connection, Catalog{first, second}};
    try
    {
        runner.up();
        FAIL() << "An infinity value must not be recorded as a successful model migration";
    }
    catch (const MigrationError& error)
    {
        EXPECT_EQ(error.getCode(), ErrorCode::InvalidSchema);
        EXPECT_EQ(error.getVersion(), 2);
    }
    EXPECT_EQ(runner.status().currentVersion, 1);
    EXPECT_FALSE(runner.status().incomplete);
    int columns{}, rows{};
    session() << "SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=current_schema() "
                 "AND table_name='migration_temporal_values' AND column_name='value'",
        soci::into(columns);
    session() << "SELECT COUNT(*) FROM " + quote("migration_temporal_values"), soci::into(rows);
    EXPECT_EQ(columns, 0);
    EXPECT_EQ(rows, 1);
}

TEST_P(TemporalMigrationLiveTest, DateBackfillOutsideProviderDomainRollsBackDdlAndHistory)
{
    if (GetParam().type != Backend::Postgres)
        GTEST_SKIP() << "PostgreSQL supplies transactional DDL for the restricted-provider regression";
    postgresql_test::PostgresqlTestSchema schema{std::get<orm::PostgresConfig>(testConnectionConfig())};
    std::vector<std::unique_ptr<orm::db::BackendProvider>> providers;
    providers.push_back(std::make_unique<LimitedDateBackend>(schema.registry(), codec::parseDate("2024-12-31")));
    orm::Database limited{schema.connectionConfig(), orm::db::CommandGeneratorFactory{std::move(providers)}};
    auto& limitedSession = temporal_detail::DatabaseAccess::session(limited);
    const auto before = capture(temporal_models::SeedSchema::view, std::array{Backend::Postgres});
    const auto after = capture(temporal_models::DateSchema::view, std::array{Backend::Postgres});
    const auto first =
        generate(1, "restricted_initial", compare(emptySnapshot(std::array{Backend::Postgres}), before)).migration;
    Runner{limited, Catalog{first}}.up();
    limitedSession << "INSERT INTO migration_temporal_values VALUES(1)";
    GenerationDecisions decisions;
    decisions.backfills.push_back(
        {.table = "migration_temporal_values", .column = "value", .constant = codec::parseDate("2025-01-01")});
    const auto second = generate(2, "restricted_date", compare(before, after, decisions), decisions).migration;
    Runner runner{limited, Catalog{first, second}};
    try
    {
        runner.up();
        FAIL() << "A native date outside the provider domain must not be accepted as an applied migration";
    }
    catch (const MigrationError& error)
    {
        EXPECT_EQ(error.getCode(), ErrorCode::InvalidSchema);
        EXPECT_EQ(error.getVersion(), 2);
        EXPECT_NE(std::string{error.what()}.find("outside the target model domain"), std::string::npos);
    }
    EXPECT_EQ(runner.status().currentVersion, 1);
    EXPECT_FALSE(runner.status().incomplete);
    int columns{}, rows{};
    limitedSession << "SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=current_schema() "
                      "AND table_name='migration_temporal_values' AND column_name='value'",
        soci::into(columns);
    limitedSession << "SELECT COUNT(*) FROM migration_temporal_values", soci::into(rows);
    EXPECT_EQ(columns, 0);
    EXPECT_EQ(rows, 1);
}

TEST_P(TemporalMigrationLiveTest, PostgresqlEndOfDayCannotBecomeATimeOfDay)
{
    if (GetParam().type != Backend::Postgres)
        GTEST_SKIP() << "PostgreSQL 24:00 is outside the public time-of-day domain";
    const auto before = capture(temporal_models::SeedSchema::view, std::array{GetParam().type});
    const auto after = capture(temporal_models::TimeSchema::view, std::array{GetParam().type});
    const auto first = initial(before);
    session() << "INSERT INTO " + quote("migration_temporal_values") + " VALUES(1)";
    GenerationDecisions decisions;
    decisions.backfills.push_back({.table = "migration_temporal_values",
                                   .column = "value",
                                   .expressions = {{Backend::Postgres, "'24:00:00'::time"}}});
    const auto second = generate(2, "invalid_time_domain", compare(before, after, decisions), decisions).migration;
    Runner runner{connection, Catalog{first, second}};
    EXPECT_THROW(runner.up(), MigrationError);
    EXPECT_EQ(runner.status().currentVersion, 1);
    EXPECT_FALSE(runner.status().incomplete);
}

INSTANTIATE_TEST_SUITE_P(DatabaseTest, TemporalMigrationLiveTest, orm::test::fixtures::conformanceBackendTestConfigs,
                         orm::test::fixtures::backendTestName);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(TemporalMigrationLiveTest);
