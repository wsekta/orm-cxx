module;

#include "soci/mysql/soci-mysql.h"
#include "tests/UnitTestPrelude.hpp"

module orm;

import :internal;
import :test_support;
import :foundation;
import :model;
import :expressions;
import :dynamic_query;
import :static_plan;
import :sql;
import :database;

TEST(MysqlBackendTest, advertisesEqualCapabilitiesWithBackendSpecificAtomicity)
{
    const orm::db::mysql::MysqlBackend backend;
    EXPECT_EQ(backend.type(), orm::db::BackendType::Mysql);
    EXPECT_EQ(backend.compiledSqlFlavor(), orm::db::CompiledSqlFlavor::MySQLFamily);
    EXPECT_TRUE(backend.acceptsConnectionString("mysql://db=test"));
    EXPECT_FALSE(backend.acceptsConnectionString("postgresql://dbname=test"));
    EXPECT_FALSE(backend.capabilities().schema.transactionalDdl);
    EXPECT_FALSE(backend.capabilities().query.maxOrderedStringLength.has_value());
    EXPECT_TRUE(backend.capabilities().schema.boundedStringKeys);
    EXPECT_TRUE(backend.capabilities().transactions);
    EXPECT_TRUE(backend.capabilities().migrations.supported);
    EXPECT_EQ(backend.capabilities().migrations.atomicity, orm::db::MigrationAtomicity::NonTransactional);
    EXPECT_FALSE(backend.capabilities().valueLimits.maxUnsignedLongLong.has_value());
    EXPECT_TRUE(backend.capabilities().supportsColumnType(orm::model::ColumnType::String));
    EXPECT_FALSE(backend.capabilities().supportsColumnType(orm::model::ColumnType::Uuid));
    soci::session session;
    EXPECT_THROW(backend.runtime().open(session, "sqlite3://:memory:"), std::invalid_argument);
    EXPECT_THROW(backend.runtime().open(session, std::string{"mysql://"} + '\0'), std::invalid_argument);
    EXPECT_THROW(backend.runtime().open(session, "mysql://reconnect=1"), std::invalid_argument);
    EXPECT_EQ(backend.runtime().normalizeAffectedRows(0), 0);
    EXPECT_EQ(backend.runtime().normalizeAffectedRows(7), 7);
    EXPECT_THROW((void)backend.runtime().normalizeAffectedRows(-1), std::runtime_error);
}

TEST(MysqlBackendTest, preservesFullUnsignedRangeAndNullBinding)
{
    const orm::db::mysql::MysqlBackend backend;
    soci::values values;
    backend.runtime().bind(values, "maximum",
                           {.logicalType = orm::model::ColumnType::UnsignedLongLong,
                            .value = orm::query::QueryValue::Value{(std::numeric_limits<unsigned long long>::max)()}});
    EXPECT_EQ(values.get<unsigned long long>("maximum"), (std::numeric_limits<unsigned long long>::max)());
    backend.runtime().bind(values, "null_value",
                           {.logicalType = orm::model::ColumnType::UnsignedLongLong, .value = std::nullopt});
    EXPECT_EQ(values.get_indicator("null_value"), soci::i_null);
}

TEST(MysqlBackendTest, normalizesNativeErrorsWithoutLeakingDriverMessage)
{
    const orm::db::mysql::MysqlBackend backend;
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
        EXPECT_EQ(error.getBackendType(), orm::db::BackendType::Mysql);
        EXPECT_EQ(error.getNativeCode(), std::optional<std::string>{std::to_string(number)});
        EXPECT_EQ(std::string_view{error.what()}.find("secret"), std::string_view::npos);
        EXPECT_EQ(backend.runtime().statementErrorInvalidatesTransaction(native),
                  number == 1213 || number == 1205 || number == 2006);
    }
}
