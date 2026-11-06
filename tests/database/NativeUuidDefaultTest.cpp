module;

#include "tests/UnitTestPrelude.hpp"

module orm;

import :internal;
import :migrations_internal;
import :test_support;

namespace native_uuid_default_models
{
struct Record
{
    int id;
    orm::Uuid value;
    inline static constexpr orm::reflection::FixedString table_name{"native_uuid_without_opt_in"};
};
using Schema = orm::Schema<Record>;
} // namespace native_uuid_default_models

class NativeUuidWithoutOptInTest : public DatabaseTest<native_uuid_default_models::Schema>
{
};

TEST_P(NativeUuidWithoutOptInTest, SqliteAndMysqlRejectNativeUuidBeforeDdl)
{
    if (GetParam().type != orm::db::BackendType::Sqlite && GetParam().type != orm::db::BackendType::Mysql)
        GTEST_SKIP() << "PostgreSQL and MariaDB support native UUID";
    try
    {
        database.createTable<native_uuid_default_models::Record>();
        FAIL() << "Native UUID requires an explicit Binary16 mapping for this backend";
    }
    catch (const orm::DatabaseError& error)
    {
        EXPECT_EQ(error.getCode(), orm::DatabaseErrorCode::UnsupportedFeature);
    }
    int tables{};
    auto& session = orm::migrations::detail::DatabaseAccess::session(connection);
    session << (GetParam().type == orm::db::BackendType::Sqlite ?
                    "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='native_uuid_without_opt_in'" :
                    "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema=DATABASE() "
                    "AND table_name='native_uuid_without_opt_in'"),
        soci::into(tables);
    EXPECT_EQ(tables, 0);
}

INSTANTIATE_TEST_SUITE_P(DatabaseTest, NativeUuidWithoutOptInTest, orm::test::fixtures::backendTestConfigs,
                         orm::test::fixtures::backendTestName);
