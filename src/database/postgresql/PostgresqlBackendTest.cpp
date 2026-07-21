#include "orm-cxx/database/postgresql/PostgresqlBackend.hpp"

#include <gtest/gtest.h>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "orm-cxx/database/BackendRuntime.hpp"
#include "orm-cxx/database/binding/ConversionError.hpp"
#include "soci/postgresql/soci-postgresql.h"
#include "soci/soci.h"

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

TEST(PostgresqlBackendTest, exposesPostgresqlIdentityAndCapabilities)
{
    const orm::db::postgresql::PostgresqlBackend backend;
    const auto& capabilities = backend.capabilities();

    EXPECT_EQ(backend.type(), orm::db::BackendType::Postgres);
    EXPECT_TRUE(backend.acceptsConnectionString("postgresql://host=localhost dbname=orm_cxx"));
    EXPECT_FALSE(backend.acceptsConnectionString("postgres://localhost/orm_cxx"));
    EXPECT_FALSE(backend.acceptsConnectionString("sqlite3://:memory:"));
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
    EXPECT_FALSE(capabilities.supportsColumnType(orm::model::ColumnType::Uuid));
    ASSERT_TRUE(capabilities.valueLimits.maxUnsignedLongLong.has_value());
    EXPECT_EQ(capabilities.valueLimits.maxUnsignedLongLong.value(),
              static_cast<unsigned long long>(std::numeric_limits<long long>::max()));
}

TEST(PostgresqlBackendTest, runtimeRejectsAConnectionStringOwnedByAnotherBackendBeforeOpening)
{
    const orm::db::postgresql::PostgresqlBackend backend;
    soci::session session;

    EXPECT_THROW(backend.runtime().open(session, "sqlite3://:memory:"), std::invalid_argument);
    EXPECT_FALSE(session.is_connected());
}

TEST(PostgresqlBackendTest, runtimeRejectsConnectionStringsContainingEmbeddedNulBeforeOpening)
{
    const orm::db::postgresql::PostgresqlBackend backend;
    soci::session session;
    const auto connectionString = std::string{"postgresql://host=localhost"} + '\0' + " dbname=orm_cxx";

    EXPECT_THROW(backend.runtime().open(session, connectionString), std::invalid_argument);
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
