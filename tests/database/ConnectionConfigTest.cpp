#include <filesystem>

#include "tests/UnitTestPrelude.hpp"

import orm;

namespace
{
struct CustomConfig
{
    std::string endpoint;
};
struct OtherCustomConfig
{
};
struct MissingTraits
{
};
}

template <>
struct orm::db::ConnectionTraits<CustomConfig>
{
    static constexpr auto backendType = orm::db::BackendType::Oracle;
};
template <>
struct orm::db::ConnectionTraits<OtherCustomConfig>
{
    static constexpr auto backendType = orm::db::BackendType::Oracle;
};

static_assert(orm::db::ConnectionConfiguration<orm::SqliteConfig>);
static_assert(orm::db::ConnectionConfiguration<orm::PostgresConfig>);
static_assert(orm::db::ConnectionConfiguration<orm::MysqlConfig>);
static_assert(orm::db::ConnectionConfiguration<orm::MariadbConfig>);
static_assert(orm::db::ConnectionConfiguration<CustomConfig>);
static_assert(not orm::db::ConnectionConfiguration<MissingTraits>);
static_assert(not orm::db::ConnectionConfiguration<const CustomConfig>);
static_assert(not std::is_constructible_v<orm::db::ConnectionConfigView, CustomConfig&&>);
static_assert(std::is_same_v<decltype(std::declval<const orm::db::ConnectionConfigView&>().get<CustomConfig>()),
                             const CustomConfig&>);

TEST(ConnectionConfigTest, preservesOmittedAndExplicitEmptyValues)
{
    auto config = orm::PostgresConfig{};
    EXPECT_FALSE(config.password.has_value());
    EXPECT_FALSE(config.port.has_value());
    EXPECT_FALSE(config.tls.mode.has_value());
    config.password = "";
    ASSERT_TRUE(config.password.has_value());
    EXPECT_TRUE(config.password->empty());
    EXPECT_TRUE(std::get<std::filesystem::path>(orm::SqliteConfig{}.database).empty());
    EXPECT_TRUE(std::holds_alternative<orm::SqliteMemory>(orm::SqliteConfig{.database = orm::SqliteMemory{}}.database));
}

TEST(ConnectionConfigTest, borrowsCustomConfigurationWithoutCopying)
{
    const auto config = CustomConfig{"private endpoint"};
    const auto view = orm::db::ConnectionConfigView{config};
    const auto copy = view;
    EXPECT_EQ(view.backendType(), orm::db::BackendType::Oracle);
    EXPECT_EQ(std::addressof(copy.get<CustomConfig>()), std::addressof(config));
}

TEST(ConnectionConfigTest, rejectsDifferentTypeEvenWithSameBackend)
{
    const auto config = CustomConfig{"secret endpoint"};
    const auto view = orm::db::ConnectionConfigView{config};
    try
    {
        (void)view.get<OtherCustomConfig>();
        FAIL() << "Expected configuration type mismatch";
    }
    catch (const orm::DatabaseError& error)
    {
        EXPECT_EQ(error.getCode(), orm::DatabaseErrorCode::Connection);
        EXPECT_EQ(error.getBackendType(), orm::db::BackendType::Oracle);
        EXPECT_EQ(error.getOperation(), "connect");
        EXPECT_STREQ(error.what(), "Connection configuration type mismatch");
        EXPECT_FALSE(error.getNativeCode().has_value());
    }
}
