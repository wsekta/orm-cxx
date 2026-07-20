#include "orm-cxx/database/sqlite/SqliteBackend.hpp"

#include <gtest/gtest.h>
#include <limits>
#include <stdexcept>

#include "orm-cxx/database/BackendRuntime.hpp"
#include "orm-cxx/database/binding/ConversionError.hpp"
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

TEST(SqliteBackendTest, exposesSQLiteIdentityAndCapabilities)
{
    const orm::db::sqlite::SqliteBackend backend;
    const auto& capabilities = backend.capabilities();

    EXPECT_EQ(backend.type(), orm::db::BackendType::Sqlite);
    EXPECT_TRUE(backend.acceptsConnectionString("sqlite3://:memory:"));
    EXPECT_FALSE(backend.acceptsConnectionString("postgresql://localhost/test"));
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
    EXPECT_TRUE(capabilities.query.collectionPredicates);
    EXPECT_TRUE(capabilities.mutations.insert);
    EXPECT_TRUE(capabilities.mutations.update);
    EXPECT_TRUE(capabilities.mutations.remove);
    EXPECT_TRUE(capabilities.mutations.atomicInsertIfAbsent);
    EXPECT_EQ(capabilities.mutations.affectedRows, orm::db::AffectedRowsSupport::Reliable);
    EXPECT_TRUE(capabilities.relations.junctionTables);
    EXPECT_TRUE(capabilities.relations.compositeEndpointKeys);
    EXPECT_TRUE(capabilities.relations.toOne);
    EXPECT_TRUE(capabilities.relations.oneToMany);
    EXPECT_TRUE(capabilities.relations.manyToMany);
    EXPECT_TRUE(capabilities.relations.collectionIncludes);
    EXPECT_TRUE(capabilities.relations.collectionPredicates);
    EXPECT_TRUE(capabilities.transactions);
    EXPECT_TRUE(capabilities.supportsColumnType(orm::model::ColumnType::String));
    EXPECT_FALSE(capabilities.supportsColumnType(orm::model::ColumnType::Uuid));
    ASSERT_TRUE(capabilities.valueLimits.maxUnsignedLongLong.has_value());
    EXPECT_EQ(capabilities.valueLimits.maxUnsignedLongLong.value(),
              static_cast<unsigned long long>(std::numeric_limits<long long>::max()));
}

TEST(SqliteBackendTest, runtimeInitializesConnectionAndInspectsSchema)
{
    const orm::db::sqlite::SqliteBackend backend;
    soci::session session;
    session.open("sqlite3://:memory:");

    backend.runtime().onConnect(session);

    int foreignKeysEnabled{};
    session << "PRAGMA foreign_keys;", soci::into(foreignKeysEnabled);
    EXPECT_EQ(foreignKeysEnabled, 1);
    EXPECT_FALSE(backend.runtime().tableExists(session, "runtime_probe"));

    session << "CREATE TABLE runtime_probe (id INTEGER);";
    EXPECT_TRUE(backend.runtime().tableExists(session, "runtime_probe"));

    const auto limits = backend.runtime().limits(session);
    ASSERT_TRUE(limits.maxBindParameters.has_value());
    EXPECT_EQ(limits.maxBindParameters.value(), 900);
}

TEST(SqliteBackendTest, runtimeNormalizesAffectedRows)
{
    const orm::db::sqlite::SqliteBackend backend;

    EXPECT_EQ(backend.runtime().normalizeAffectedRows(0), 0);
    EXPECT_EQ(backend.runtime().normalizeAffectedRows(2), 2);
    EXPECT_THROW((void)backend.runtime().normalizeAffectedRows(-1), std::runtime_error);
}

TEST(SqliteBackendTest, runtimeAdaptsUnsigned64BitValuesToTheLosslessSQLiteRange)
{
    const orm::db::sqlite::SqliteBackend backend;
    const auto maximum = static_cast<unsigned long long>(std::numeric_limits<long long>::max());
    auto values = soci::values{};

    backend.runtime().bind(values, "maximum",
                           orm::db::BoundValue{.logicalType = orm::model::ColumnType::UnsignedLongLong,
                                               .value = orm::query::QueryValue::Value{maximum}});

    EXPECT_EQ(values.get<long long>("maximum"), std::numeric_limits<long long>::max());

    EXPECT_THROW(backend.runtime().bind(values, "too_large",
                                        orm::db::BoundValue{.logicalType = orm::model::ColumnType::UnsignedLongLong,
                                                            .value = orm::query::QueryValue::Value{maximum + 1}}),
                 orm::db::binding::ConversionError);
}

TEST(SqliteBackendTest, runtimeRejectsMismatchedUnsigned64BitStorage)
{
    const orm::db::sqlite::SqliteBackend backend;
    auto values = soci::values{};

    EXPECT_THROW(backend.runtime().bind(values, "mismatched",
                                        orm::db::BoundValue{
                                            .logicalType = orm::model::ColumnType::UnsignedLongLong,
                                            .value = orm::query::QueryValue::Value{42},
                                        }),
                 orm::db::binding::ConversionError);
}

TEST(SqliteBackendTest, runtimeTranslatesEveryPortableSociErrorCategory)
{
    const orm::db::sqlite::SqliteBackend backend;

    const auto expectCode = [&backend](soci::soci_error::error_category category, orm::DatabaseErrorCode expected)
    {
        const auto translated = backend.runtime().translateError(CategorizedSociError{category},
                                                                 orm::DatabaseErrorCode::Statement, "query");
        EXPECT_EQ(translated.getCode(), expected);
        EXPECT_EQ(translated.getBackendType(), orm::db::BackendType::Sqlite);
        EXPECT_EQ(translated.getOperation(), "query");
        EXPECT_FALSE(translated.getNativeCode().has_value());
    };

    expectCode(soci::soci_error::connection_error, orm::DatabaseErrorCode::Connection);
    expectCode(soci::soci_error::constraint_violation, orm::DatabaseErrorCode::Constraint);
    expectCode(soci::soci_error::unknown_transaction_state, orm::DatabaseErrorCode::Transaction);
    expectCode(soci::soci_error::invalid_statement, orm::DatabaseErrorCode::Statement);
    expectCode(soci::soci_error::no_privilege, orm::DatabaseErrorCode::Statement);
    expectCode(soci::soci_error::no_data, orm::DatabaseErrorCode::Statement);
    expectCode(soci::soci_error::system_error, orm::DatabaseErrorCode::Statement);
    expectCode(soci::soci_error::unknown, orm::DatabaseErrorCode::Statement);
}
