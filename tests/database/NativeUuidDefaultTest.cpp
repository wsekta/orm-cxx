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
struct KeyRecord
{
    orm::Uuid id;
    inline static constexpr orm::reflection::FixedString table_name{"native_uuid_key_without_opt_in"};
};

struct RelatedRecord
{
    int id;
    KeyRecord target;
    inline static constexpr orm::reflection::FixedString table_name{"native_uuid_relation_without_opt_in"};
};

using Schema = orm::Schema<Record, KeyRecord, RelatedRecord>;
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

TEST_P(NativeUuidWithoutOptInTest, SqliteAndMysqlRejectAnUnsupportedRelatedKeyBeforeDdl)
{
    if (GetParam().type != orm::db::BackendType::Sqlite && GetParam().type != orm::db::BackendType::Mysql)
        GTEST_SKIP() << "PostgreSQL and MariaDB support native UUID";

    // A to-one field legitimately has no scalar type. Validation must inspect its
    // target's typed primary key instead of treating the relation as a scalar.
    constexpr auto owner =
        orm::modelView<native_uuid_default_models::Schema, native_uuid_default_models::RelatedRecord>();
    ASSERT_EQ(owner->columns[1].kind, orm::model::FieldKind::ToOne);
    EXPECT_FALSE(owner->columns[1].type.has_value());
    try
    {
        database.createTable<native_uuid_default_models::RelatedRecord>();
        FAIL() << "A related native UUID key requires an explicit representation for this backend";
    }
    catch (const orm::DatabaseError& error)
    {
        EXPECT_EQ(error.getCode(), orm::DatabaseErrorCode::UnsupportedFeature);
        EXPECT_EQ(error.getBackendType(), GetParam().type);
        EXPECT_EQ(error.getOperation(), "create table");
        EXPECT_EQ(std::string_view{error.what()}, "A related primary-key column type is not supported by the backend");
    }

    int tables{};
    auto& session = orm::migrations::detail::DatabaseAccess::session(connection);
    session << (GetParam().type == orm::db::BackendType::Sqlite ?
                    "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND "
                    "name IN ('native_uuid_key_without_opt_in', 'native_uuid_relation_without_opt_in')" :
                    "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema=DATABASE() AND "
                    "table_name IN ('native_uuid_key_without_opt_in', 'native_uuid_relation_without_opt_in')"),
        soci::into(tables);
    EXPECT_EQ(tables, 0);
    EXPECT_TRUE(connection.isConnected());
}

INSTANTIATE_TEST_SUITE_P(DatabaseTest, NativeUuidWithoutOptInTest, orm::test::fixtures::backendTestConfigs,
                         orm::test::fixtures::backendTestName);
