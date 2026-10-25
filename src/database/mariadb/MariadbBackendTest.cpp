module;

#include "soci/mysql/soci-mysql.h"
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

TEST(MariadbBackendTest, advertisesEqualCapabilitiesWithBackendSpecificAtomicity)
{
    const orm::db::mariadb::MariadbBackend backend;
    EXPECT_EQ(backend.type(), orm::db::BackendType::Mariadb);
    EXPECT_EQ(backend.compiledSqlFlavor(), orm::db::CompiledSqlFlavor::MySQLFamily);
    EXPECT_FALSE(backend.capabilities().schema.transactionalDdl);
    EXPECT_EQ(backend.capabilities().query.maxOrderedStringLength, std::optional<std::size_t>{16'383});
    EXPECT_TRUE(backend.capabilities().schema.boundedStringKeys);
    EXPECT_TRUE(backend.capabilities().transactions);
    EXPECT_TRUE(backend.capabilities().migrations.supported);
    EXPECT_EQ(backend.capabilities().migrations.atomicity, orm::db::MigrationAtomicity::NonTransactional);
    EXPECT_FALSE(backend.capabilities().valueLimits.maxUnsignedLongLong.has_value());
    EXPECT_TRUE(backend.capabilities().supportsColumnType(orm::model::ColumnType::String));
    EXPECT_TRUE(backend.capabilities().supportsColumnType(orm::model::ColumnType::Uuid));
    EXPECT_TRUE(backend.capabilities().supportsColumnType(orm::model::ColumnType::Date));
    EXPECT_TRUE(backend.capabilities().supportsColumnType(orm::model::ColumnType::TimeOfDay));
    EXPECT_TRUE(backend.capabilities().supportsColumnType(orm::model::ColumnType::LocalDateTime));
    EXPECT_TRUE(backend.capabilities().supportsColumnType(orm::model::ColumnType::UtcTimestamp));
    soci::session session;
    const orm::PostgresConfig mismatched;
    EXPECT_THROW(backend.runtime().open(session, orm::db::ConnectionConfigView{mismatched}), orm::DatabaseError);
    const orm::MariadbConfig invalid{.password = std::string{"private"} + '\0'};
    EXPECT_THROW(backend.runtime().open(session, orm::db::ConnectionConfigView{invalid}), orm::DatabaseError);
    EXPECT_EQ(backend.runtime().normalizeAffectedRows(0), 0);
    EXPECT_EQ(backend.runtime().normalizeAffectedRows(7), 7);
    EXPECT_THROW((void)backend.runtime().normalizeAffectedRows(-1), std::runtime_error);
}

TEST(MariadbBackendTest, preservesFullUnsignedRangeAndNullBinding)
{
    const orm::db::mariadb::MariadbBackend backend;
    soci::values values;
    backend.runtime().bind(values, "maximum",
                           {.logicalType = orm::model::ColumnType::UnsignedLongLong,
                            .value = orm::query::QueryValue::Value{(std::numeric_limits<unsigned long long>::max)()}});
    EXPECT_EQ(values.get<unsigned long long>("maximum"), (std::numeric_limits<unsigned long long>::max)());
    backend.runtime().bind(values, "null_value",
                           {.logicalType = orm::model::ColumnType::UnsignedLongLong, .value = std::nullopt});
    EXPECT_EQ(values.get_indicator("null_value"), soci::i_null);
}

TEST(MariadbBackendTest, normalizesNativeErrorsWithoutLeakingDriverMessage)
{
    const orm::db::mariadb::MariadbBackend backend;
    const std::array<std::pair<int, orm::DatabaseErrorCode>, 9> cases{{{1062, orm::DatabaseErrorCode::Constraint},
                                                                       {1452, orm::DatabaseErrorCode::Constraint},
                                                                       {1213, orm::DatabaseErrorCode::Transaction},
                                                                       {1205, orm::DatabaseErrorCode::Transaction},
                                                                       {1045, orm::DatabaseErrorCode::Connection},
                                                                       {2006, orm::DatabaseErrorCode::Connection},
                                                                       {1264, orm::DatabaseErrorCode::Conversion},
                                                                       {1406, orm::DatabaseErrorCode::Conversion},
                                                                       {1064, orm::DatabaseErrorCode::Statement}}};
    for (const auto& [number, code] : cases)
    {
        const soci::mysql_soci_error native{"password=secret", number};
        const auto error = backend.runtime().translateError(native, orm::DatabaseErrorCode::Statement, "update");
        EXPECT_EQ(error.getCode(), code);
        EXPECT_EQ(error.getBackendType(), orm::db::BackendType::Mariadb);
        EXPECT_EQ(error.getNativeCode(), std::optional<std::string>{std::to_string(number)});
        EXPECT_EQ(std::string_view{error.what()}.find("secret"), std::string_view::npos);
        EXPECT_EQ(backend.runtime().statementErrorInvalidatesTransaction(native),
                  number == 1213 || number == 1205 || number == 2006);
    }
}
