module;

#include <libpq-fe.h>

#include "soci/postgresql/soci-postgresql.h"
#include "tests/UnitTestPrelude.hpp"

module orm;

import :internal;
import :connection_config;
import :test_support;
import :foundation;
import :model;
import :expressions;
import :dynamic_query;
import :static_plan;
import :sql;
import :database;
import :value_codec;
import :migrations_internal;

namespace
{
class CategorizedSociError final : public soci::soci_error
{
public:
    explicit CategorizedSociError(error_category categoryInit)
        : soci_error{"categorized test error"}, category{categoryInit}
    {
    }

    [[nodiscard]] auto get_error_category() const -> error_category override
    {
        return category;
    }

private:
    error_category category;
};
} // namespace

TEST(PostgresqlBackendTest, nativeNullResultsReportFailureAndPermitCleanup)
{
    EXPECT_EQ(PQresultStatus(nullptr), PGRES_FATAL_ERROR);
    EXPECT_EQ(PQresultErrorField(nullptr, PG_DIAG_SQLSTATE), nullptr);
    PQclear(nullptr);
}

TEST(PostgresqlBackendTest, exposesPostgresqlIdentityAndCapabilities)
{
    const orm::db::postgresql::PostgresqlBackend backend;
    const auto& capabilities = backend.capabilities();

    EXPECT_EQ(backend.type(), orm::db::BackendType::Postgres);
    EXPECT_TRUE(capabilities.schema.createTableIfNotExists);
    EXPECT_TRUE(capabilities.schema.dropTableIfExists);
    EXPECT_TRUE(capabilities.schema.autoIncrementPrimaryKey);
    EXPECT_TRUE(capabilities.schema.compositePrimaryKeys);
    EXPECT_TRUE(capabilities.schema.foreignKeys);
    EXPECT_TRUE(capabilities.schema.onDeleteCascade);
    EXPECT_TRUE(capabilities.query.limit);
    EXPECT_TRUE(capabilities.query.offset);
    EXPECT_TRUE(capabilities.query.offsetWithoutLimit);
    EXPECT_FALSE(capabilities.query.offsetRequiresOrderBy);
    EXPECT_TRUE(capabilities.query.projections);
    EXPECT_TRUE(capabilities.query.groupBy);
    EXPECT_TRUE(capabilities.query.having);
    EXPECT_FALSE(capabilities.query.fullModelGrouping);
    EXPECT_TRUE(capabilities.query.distinctOrderByRequiresProjectedColumn);
    EXPECT_TRUE(capabilities.query.strictProjectionGrouping);
    EXPECT_TRUE(capabilities.query.collectionPredicates);
    EXPECT_TRUE(capabilities.mutations.insert);
    EXPECT_TRUE(capabilities.mutations.update);
    EXPECT_TRUE(capabilities.mutations.remove);
    EXPECT_TRUE(capabilities.mutations.atomicInsertIfAbsent);
    EXPECT_EQ(capabilities.mutations.affectedRows, orm::db::AffectedRowsSupport::Reliable);
    EXPECT_TRUE(capabilities.relations.toOne);
    EXPECT_TRUE(capabilities.relations.oneToMany);
    EXPECT_TRUE(capabilities.relations.manyToMany);
    EXPECT_TRUE(capabilities.relations.collectionIncludes);
    EXPECT_TRUE(capabilities.relations.collectionPredicates);
    EXPECT_TRUE(capabilities.relations.junctionTables);
    EXPECT_TRUE(capabilities.relations.compositeEndpointKeys);
    EXPECT_TRUE(capabilities.transactions);
    EXPECT_TRUE(capabilities.supportsColumnType(orm::model::ColumnType::String));
    EXPECT_TRUE(capabilities.supportsColumnType(orm::model::ColumnType::Uuid));
    EXPECT_TRUE(capabilities.supportsColumnType(orm::model::ColumnType::Date));
    EXPECT_TRUE(capabilities.supportsColumnType(orm::model::ColumnType::TimeOfDay));
    EXPECT_TRUE(capabilities.supportsColumnType(orm::model::ColumnType::LocalDateTime));
    EXPECT_TRUE(capabilities.supportsColumnType(orm::model::ColumnType::UtcTimestamp));
    EXPECT_EQ(capabilities.valueLimits.minDate,
              (std::optional<orm::Date>{orm::Date{std::chrono::year{1}, std::chrono::month{1}, std::chrono::day{1}}}));
    ASSERT_TRUE(capabilities.valueLimits.maxUnsignedLongLong.has_value());
    EXPECT_EQ(capabilities.valueLimits.maxUnsignedLongLong.value(),
              static_cast<unsigned long long>(std::numeric_limits<long long>::max()));
}

TEST(PostgresqlBackendTest, runtimeRejectsAConfigurationOwnedByAnotherBackendBeforeOpening)
{
    const orm::db::postgresql::PostgresqlBackend backend;
    soci::session session;

    const auto config = orm::SqliteConfig{orm::SqliteMemory{}};
    EXPECT_THROW(backend.runtime().open(session, orm::db::ConnectionConfigView{config}), orm::DatabaseError);
    EXPECT_FALSE(session.is_connected());
}

TEST(PostgresqlBackendTest, runtimeRejectsConfigurationsContainingEmbeddedNulBeforeOpening)
{
    const orm::db::postgresql::PostgresqlBackend backend;
    soci::session session;
    const auto config = orm::PostgresConfig{.host = std::string{"localhost"} + '\0' + "ignored"};

    EXPECT_THROW(backend.runtime().open(session, orm::db::ConnectionConfigView{config}), orm::DatabaseError);
    EXPECT_FALSE(session.is_connected());
}

TEST(PostgresqlBackendTest, runtimeExposesProtocolBindLimitAndNormalizesAffectedRows)
{
    const orm::db::postgresql::PostgresqlBackend backend;
    soci::session session;
    const auto limits = backend.runtime().limits(session);

    ASSERT_TRUE(limits.maxBindParameters.has_value());
    EXPECT_EQ(limits.maxBindParameters.value(), 65'535);
    EXPECT_EQ(backend.runtime().normalizeAffectedRows(0), 0);
    EXPECT_EQ(backend.runtime().normalizeAffectedRows(2), 2);
    EXPECT_THROW((void)backend.runtime().normalizeAffectedRows(-1), std::runtime_error);
}

TEST(PostgresqlBackendTest, runtimeInvalidatesTransactionsOnlyForErrorsReportedByPostgresql)
{
    const orm::db::postgresql::PostgresqlBackend backend;
    const soci::postgresql_soci_error serverError{"server statement error", "42601"};
    const CategorizedSociError clientError{soci::soci_error::invalid_statement};

    EXPECT_TRUE(backend.runtime().statementErrorInvalidatesTransaction(serverError));
    EXPECT_FALSE(backend.runtime().statementErrorInvalidatesTransaction(clientError));
}

TEST(PostgresqlBackendTest, runtimeAdaptsUnsigned64BitValuesToTheLosslessBigintRange)
{
    const orm::db::postgresql::PostgresqlBackend backend;
    const auto maximum = static_cast<unsigned long long>(std::numeric_limits<long long>::max());
    auto values = soci::values{};

    backend.runtime().bind(values, "maximum",
                           orm::db::BoundValue{.logicalType = orm::model::ColumnType::UnsignedLongLong,
                                               .value = orm::query::QueryValue::Value{maximum}});
    backend.runtime().bind(
        values, "missing",
        orm::db::BoundValue{.logicalType = orm::model::ColumnType::UnsignedLongLong, .value = std::nullopt});

    EXPECT_EQ(values.get<long long>("maximum"), std::numeric_limits<long long>::max());
    EXPECT_EQ(values.get_indicator("missing"), soci::i_null);
    EXPECT_THROW(backend.runtime().bind(values, "too_large",
                                        orm::db::BoundValue{.logicalType = orm::model::ColumnType::UnsignedLongLong,
                                                            .value = orm::query::QueryValue::Value{maximum + 1}}),
                 orm::db::binding::ConversionError);
}

TEST(PostgresqlBackendTest, runtimeRejectsMismatchedUnsigned64BitStorage)
{
    const orm::db::postgresql::PostgresqlBackend backend;
    auto values = soci::values{};

    EXPECT_THROW(backend.runtime().bind(values, "mismatched",
                                        orm::db::BoundValue{
                                            .logicalType = orm::model::ColumnType::UnsignedLongLong,
                                            .value = orm::query::QueryValue::Value{42},
                                        }),
                 orm::db::binding::ConversionError);
}

TEST(PostgresqlBackendTest, runtimeRejectsStringValuesContainingEmbeddedNul)
{
    const orm::db::postgresql::PostgresqlBackend backend;
    auto values = soci::values{};
    const auto embeddedNul = std::string{"before"} + '\0' + "after";

    EXPECT_THROW(backend.runtime().bind(values, "text",
                                        orm::db::BoundValue{
                                            .logicalType = orm::model::ColumnType::String,
                                            .value = orm::query::QueryValue::Value{embeddedNul},
                                        }),
                 orm::db::binding::ConversionError);
}

TEST(PostgresqlBackendTest, runtimeRejectsMismatchedStringStorageAndAcceptsNullText)
{
    const orm::db::postgresql::PostgresqlBackend backend;
    soci::values values;

    EXPECT_THROW(backend.runtime().bind(values, "mismatched",
                                        orm::db::BoundValue{
                                            .logicalType = orm::model::ColumnType::String,
                                            .value = orm::query::QueryValue::Value{42},
                                        }),
                 orm::db::binding::ConversionError);
    backend.runtime().bind(values, "missing_text",
                           orm::db::BoundValue{.logicalType = orm::model::ColumnType::String, .value = std::nullopt});
    EXPECT_EQ(values.get_indicator("missing_text"), soci::i_null);
}

TEST(PostgresqlBackendTest, nativeValuesBindAsLosslessTextWithAnExplicitUtcOffset)
{
    const orm::db::postgresql::PostgresqlBackend backend;
    soci::values values;
    const std::array<std::pair<orm::model::ColumnType, orm::query::QueryValue::Value>, 5> inputs{{
        {orm::model::ColumnType::Date, orm::detail::values::parseDate("2024-02-29")},
        {orm::model::ColumnType::TimeOfDay, orm::detail::values::parseTimeOfDay("23:59:59.123456")},
        {orm::model::ColumnType::LocalDateTime, orm::detail::values::parseLocalDateTime("2024-02-29 23:59:59.123456")},
        {orm::model::ColumnType::UtcTimestamp,
         orm::detail::values::parseUtcTimestamp("2024-02-29 23:59:59.123456+00:00")},
        {orm::model::ColumnType::Uuid, orm::Uuid::parse("123e4567-e89b-12d3-a456-426655440000")},
    }};
    const std::array<std::string_view, 5> expected{"2024-02-29", "23:59:59.123456", "2024-02-29 23:59:59.123456",
                                                   "2024-02-29 23:59:59.123456+00:00",
                                                   "123e4567-e89b-12d3-a456-426655440000"};
    for (std::size_t index = 0; index < inputs.size(); ++index)
    {
        const auto& [type, value] = inputs[index];
        const auto name = "value_" + std::to_string(index);
        backend.runtime().bind(values, name, {.logicalType = type, .value = value});
        EXPECT_EQ(values.get<std::string>(name), expected[index]);
        const auto nullName = "null_" + std::to_string(index);
        backend.runtime().bind(values, nullName, {.logicalType = type, .value = std::nullopt});
        EXPECT_EQ(values.get_indicator(nullName), soci::i_null);
        EXPECT_THROW(backend.runtime().bind(values, "mismatch", {.logicalType = type, .value = std::string{"text"}}),
                     orm::db::binding::ConversionError);
    }
    const auto invalidDate = orm::Date{std::chrono::year{2024}, std::chrono::month{2}, std::chrono::day{30}};
    EXPECT_THROW(backend.runtime().bind(values, "invalid_date",
                                        {.logicalType = orm::model::ColumnType::Date, .value = invalidDate}),
                 orm::db::binding::ConversionError);
}

TEST(PostgresqlBackendTest, runtimeMapsSqlStateClassesAndReturnsSanitizedErrors)
{
    const orm::db::postgresql::PostgresqlBackend backend;

    const auto expectSqlState = [&backend](std::string_view sqlState, orm::DatabaseErrorCode expected)
    {
        const auto state = std::string{sqlState};
        const auto translated = backend.runtime().translateError(
            soci::postgresql_soci_error{"raw error with password=secret", state.c_str()},
            orm::DatabaseErrorCode::Statement, "query");

        EXPECT_EQ(translated.getCode(), expected);
        EXPECT_EQ(translated.getBackendType(), orm::db::BackendType::Postgres);
        EXPECT_EQ(translated.getOperation(), "query");
        ASSERT_TRUE(translated.getNativeCode().has_value());
        EXPECT_EQ(translated.getNativeCode().value(), sqlState);
        EXPECT_EQ(std::string_view{translated.what()}, "PostgreSQL operation failed: query");
        EXPECT_EQ(std::string_view{translated.what()}.find("secret"), std::string_view::npos);
    };

    expectSqlState("08006", orm::DatabaseErrorCode::Connection);
    expectSqlState("57P01", orm::DatabaseErrorCode::Connection);
    expectSqlState("57P02", orm::DatabaseErrorCode::Connection);
    expectSqlState("57P03", orm::DatabaseErrorCode::Connection);
    expectSqlState("57P04", orm::DatabaseErrorCode::Connection);
    expectSqlState("23505", orm::DatabaseErrorCode::Constraint);
    expectSqlState("22008", orm::DatabaseErrorCode::Conversion);
    expectSqlState("22P02", orm::DatabaseErrorCode::Conversion);
    expectSqlState("25006", orm::DatabaseErrorCode::Transaction);
    expectSqlState("40001", orm::DatabaseErrorCode::Transaction);
    expectSqlState("57014", orm::DatabaseErrorCode::Statement);
    expectSqlState("42601", orm::DatabaseErrorCode::Statement);
}

TEST(PostgresqlBackendTest, runtimeOmitsBlankSqlStateFromTranslatedError)
{
    const orm::db::postgresql::PostgresqlBackend backend;
    const auto translated = backend.runtime().translateError(
        soci::postgresql_soci_error{"error without SQLSTATE", "     "}, orm::DatabaseErrorCode::Statement, "query");

    EXPECT_EQ(translated.getCode(), orm::DatabaseErrorCode::Statement);
    EXPECT_FALSE(translated.getNativeCode().has_value());
}

TEST(PostgresqlBackendTest, runtimeTranslatesPortableSociCategoriesWithoutInventingNativeCodes)
{
    const orm::db::postgresql::PostgresqlBackend backend;

    const auto expectCode = [&backend](soci::soci_error::error_category category, orm::DatabaseErrorCode expected)
    {
        const auto translated = backend.runtime().translateError(CategorizedSociError{category},
                                                                 orm::DatabaseErrorCode::Statement, "query");
        EXPECT_EQ(translated.getCode(), expected);
        EXPECT_EQ(translated.getBackendType(), orm::db::BackendType::Postgres);
        EXPECT_FALSE(translated.getNativeCode().has_value());
    };

    expectCode(soci::soci_error::connection_error, orm::DatabaseErrorCode::Connection);
    expectCode(soci::soci_error::constraint_violation, orm::DatabaseErrorCode::Constraint);
    expectCode(soci::soci_error::unknown_transaction_state, orm::DatabaseErrorCode::Transaction);
    expectCode(soci::soci_error::invalid_statement, orm::DatabaseErrorCode::Statement);
    expectCode(soci::soci_error::unknown, orm::DatabaseErrorCode::Statement);
}

namespace
{
class PostgresqlNativeTemporalTest : public DatabaseTest<>
{
public:
    auto session() -> soci::session&
    {
        return orm::migrations::detail::DatabaseAccess::session(connection);
    }
    auto runtime() -> const orm::db::BackendRuntime&
    {
        return orm::migrations::detail::DatabaseAccess::runtime(connection);
    }
};
}

TEST_P(PostgresqlNativeTemporalTest, NativeTextExchangeKeepsMicrosecondsUtcAndUuid)
{
    std::string style;
    std::string zone;
    session() << "SELECT current_setting('DateStyle'),current_setting('TimeZone')", soci::into(style), soci::into(zone);
    EXPECT_EQ(style, "ISO, YMD");
    EXPECT_EQ(zone, "UTC");
    session() << "CREATE TABLE native_temporal_transport(d DATE,t TIME(6) WITHOUT TIME ZONE,"
                 "l TIMESTAMP(6) WITHOUT TIME ZONE,u TIMESTAMP(6) WITH TIME ZONE,id UUID PRIMARY KEY)";
    soci::values input;
    runtime().bind(
        input, "d",
        {.logicalType = orm::model::ColumnType::Date, .value = orm::detail::values::parseDate("2024-02-29")});
    runtime().bind(input, "t",
                   {.logicalType = orm::model::ColumnType::TimeOfDay,
                    .value = orm::detail::values::parseTimeOfDay("23:59:59.123456")});
    runtime().bind(input, "l",
                   {.logicalType = orm::model::ColumnType::LocalDateTime,
                    .value = orm::detail::values::parseLocalDateTime("2024-02-29 23:59:59.123456")});
    runtime().bind(input, "u",
                   {.logicalType = orm::model::ColumnType::UtcTimestamp,
                    .value = orm::detail::values::parseUtcTimestamp("2024-02-29 23:59:59.123456+00:00")});
    runtime().bind(input, "id",
                   {.logicalType = orm::model::ColumnType::Uuid,
                    .value = orm::Uuid::parse("123e4567-e89b-12d3-a456-426655440000")});
    session() << "INSERT INTO native_temporal_transport VALUES(:d,:t,:l,:u,:id)", soci::use(input);
    std::string date;
    std::string time;
    std::string local;
    std::string utc;
    std::string uuid;
    session() << "SELECT d,t,l,u,id FROM native_temporal_transport", soci::into(date), soci::into(time),
        soci::into(local), soci::into(utc), soci::into(uuid);
    EXPECT_EQ(date, "2024-02-29");
    EXPECT_EQ(time, "23:59:59.123456");
    EXPECT_EQ(local, "2024-02-29 23:59:59.123456");
    EXPECT_EQ(orm::detail::values::parseUtcTimestamp(utc),
              orm::detail::values::parseUtcTimestamp("2024-02-29 23:59:59.123456+00:00"));
    EXPECT_EQ(uuid, "123e4567-e89b-12d3-a456-426655440000");
    session()
        << "CREATE TABLE native_uuid_child(id INT PRIMARY KEY,owner UUID REFERENCES native_temporal_transport(id))";
    soci::values owner;
    runtime().bind(owner, "owner", {.logicalType = orm::model::ColumnType::Uuid, .value = orm::Uuid::parse(uuid)});
    session() << "INSERT INTO native_uuid_child VALUES(1,:owner)", soci::use(owner);
    int count{};
    session() << "SELECT COUNT(*) FROM native_uuid_child", soci::into(count);
    EXPECT_EQ(count, 1);
    connection.disconnect();
    connectTestDatabase(connection);
    session() << "SELECT current_setting('DateStyle'),current_setting('TimeZone')", soci::into(style), soci::into(zone);
    EXPECT_EQ(style, "ISO, YMD");
    EXPECT_EQ(zone, "UTC");
}

TEST_P(PostgresqlNativeTemporalTest, MigrationGuardsRejectNonIsoDatesAndNonUtcSessions)
{
    for (const auto setting : {"SET DateStyle TO 'SQL, DMY'", "SET TimeZone TO 'Europe/Warsaw'"})
    {
        session() << std::string{setting};
        EXPECT_THROW(runtime().validateMigrationSession(session()), orm::migrations::MigrationError);
        runtime().onConnect(session());
        EXPECT_NO_THROW(runtime().validateMigrationSession(session()));
        const auto history = runtime().migrationTable(session());
        runtime().beginMigration(session(), history);
        EXPECT_THROW(runtime().executeMigrationScript(session(), setting), orm::migrations::MigrationError);
        session().rollback();
        EXPECT_NO_THROW(runtime().validateMigrationSession(session()));
    }
}

INSTANTIATE_TEST_SUITE_P(DatabaseTest, PostgresqlNativeTemporalTest,
                         ::testing::ValuesIn(
                             []
                             {
                                 std::vector<BackendTestConfig> result;
                                 if constexpr (orm::test::config::postgresqlIntegrationEnabled)
                                     result.push_back({orm::db::BackendType::Postgres, "Postgresql", true});
                                 return result;
                             }()),
                         orm::test::fixtures::backendTestName);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(PostgresqlNativeTemporalTest);
