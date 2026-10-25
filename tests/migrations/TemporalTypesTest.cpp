module;

#include "tests/UnitTestPrelude.hpp"

module orm;

import :internal;
import :test_support;
import :migrations_internal;
import :value_codec;

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
using SeedSchema = orm::Schema<Seed>;
using ValueSchema = orm::Schema<Values>;
using DateSchema = orm::Schema<DateOnly>;
using TimeSchema = orm::Schema<TimeOnly>;
using UuidValueSchema = orm::Schema<UuidOnly>;
using UuidSchema = orm::Schema<UuidOwner, UuidTarget>;
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
