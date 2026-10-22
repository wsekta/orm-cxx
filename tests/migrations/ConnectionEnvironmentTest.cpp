module;
#include <filesystem>

#include "tests/UnitTestPrelude.hpp"
module orm;
import :internal;
import :connection_config;
import :migrations_internal;

namespace orm::test
{
auto setConnectionEnvironment(const char* key, const std::optional<std::string>& value) -> void;
}

namespace
{
using Environment = orm::migrations::detail::ConnectionEnvironment;
auto parse(const Environment& environment) -> orm::migrations::detail::ConnectionConfiguration
{
    return orm::migrations::detail::connectionConfiguration(environment);
}
auto sqlite() -> Environment
{
    return {{"ORM_CXX_DATABASE_BACKEND", "sqlite"}, {"ORM_CXX_DATABASE_NAME", ":memory:"}};
}
class ProcessBackend
{
public:
    ProcessBackend() : previous{orm::migrations::detail::connectionEnvironmentFromProcess()}
    {
        orm::test::setConnectionEnvironment("ORM_CXX_DATABASE_BACKEND", "postgresql");
    }
    ~ProcessBackend()
    {
        const auto found = previous.find("ORM_CXX_DATABASE_BACKEND");
        orm::test::setConnectionEnvironment("ORM_CXX_DATABASE_BACKEND", found == previous.end() ?
                                                                            std::nullopt :
                                                                            std::optional<std::string>{found->second});
    }

private:
    Environment previous;
};
}

TEST(MigrationConnectionEnvironmentTest, readsPresentProcessValuesAndRestoresTheEnvironment)
{
    const auto previous = orm::migrations::detail::connectionEnvironmentFromProcess();
    {
        const ProcessBackend backend;
        const auto environment = orm::migrations::detail::connectionEnvironmentFromProcess();
        EXPECT_EQ(environment.at("ORM_CXX_DATABASE_BACKEND"), "postgresql");
    }
    EXPECT_EQ(orm::migrations::detail::connectionEnvironmentFromProcess(), previous);
}

TEST(MigrationConnectionEnvironmentTest, requiresBackendAndExplicitSqliteSource)
{
    for (const auto& environment :
         std::vector<Environment>{{},
                                  {{"ORM_CXX_DATABASE_BACKEND", ""}},
                                  {{"ORM_CXX_DATABASE_BACKEND", "unknown"}},
                                  {{"ORM_CXX_DATABASE_BACKEND", "sqlite"}},
                                  {{"ORM_CXX_DATABASE_BACKEND", "sqlite"}, {"ORM_CXX_DATABASE_NAME", ""}}})
        EXPECT_THROW((void)parse(environment), std::invalid_argument);
    EXPECT_TRUE(std::holds_alternative<orm::SqliteMemory>(std::get<orm::SqliteConfig>(parse(sqlite())).database));
    auto environment = sqlite();
    const std::string file{"data = '\\ \xc5\xbc\xc3\xb3\xc5\x82\xc4\x87.sqlite"};
    environment["ORM_CXX_DATABASE_NAME"] = file;
    const auto config = std::get<orm::SqliteConfig>(parse(environment));
    const auto utf8 = std::get<std::filesystem::path>(config.database).u8string();
    EXPECT_TRUE((std::string{reinterpret_cast<const char*>(utf8.data()), utf8.size()} == file));
}

TEST(MigrationConnectionEnvironmentTest, preservesOmittedAndPresentEmptyServerFields)
{
    for (const auto* backend : {"postgresql", "mysql", "mariadb"})
    {
        Environment environment{{"ORM_CXX_DATABASE_BACKEND", backend}};
        const auto inspect = [](const auto& config, bool present)
        {
            using Config = std::decay_t<decltype(config)>;
            if constexpr (!std::is_same_v<Config, orm::SqliteConfig>)
            {
                for (const auto* field : {&config.host, &config.database, &config.user, &config.password})
                {
                    EXPECT_EQ(field->has_value(), present);
                    if (present)
                        EXPECT_TRUE(field->value().empty());
                }
                EXPECT_FALSE(config.connectTimeout);
                EXPECT_FALSE(config.port);
                EXPECT_FALSE(config.tls.mode);
            }
        };
        std::visit([&](const auto& config) { inspect(config, false); }, parse(environment));
        for (const auto* field : {"NAME", "HOST", "USER", "PASSWORD"})
            environment["ORM_CXX_DATABASE_" + std::string{field}] = "";
        std::visit([&](const auto& config) { inspect(config, true); }, parse(environment));
    }
}

TEST(MigrationConnectionEnvironmentTest, validatesEntireNumericInputAndBoundaries)
{
    for (const auto* field : {"PORT", "CONNECT_TIMEOUT_SECONDS"})
    {
        Environment environment{{"ORM_CXX_DATABASE_BACKEND", "postgresql"}};
        const auto name = "ORM_CXX_DATABASE_" + std::string{field};
        for (const auto* bad : {"", "-1", "+1", " 1", "1 ", "1x", "1.5", "18446744073709551616"})
        {
            environment[name] = bad;
            EXPECT_THROW((void)parse(environment), std::invalid_argument);
        }
        environment[name] = "0";
        EXPECT_NO_THROW((void)parse(environment));
        environment[name] =
            std::string_view{field} == "PORT" ? "65535" : std::to_string(std::numeric_limits<int>::max());
        EXPECT_NO_THROW((void)parse(environment));
        environment[name] = std::string_view{field} == "PORT" ?
                                "65536" :
                                std::to_string(static_cast<long long>(std::numeric_limits<int>::max()) + 1);
        EXPECT_THROW((void)parse(environment), std::invalid_argument);
    }
    for (const auto* backend : {"mysql", "mariadb"})
    {
        Environment environment{{"ORM_CXX_DATABASE_BACKEND", backend}};
        for (const auto value : {1ULL, static_cast<unsigned long long>(std::numeric_limits<unsigned int>::max())})
        {
            environment["ORM_CXX_DATABASE_CONNECT_TIMEOUT_SECONDS"] = std::to_string(value);
            EXPECT_NO_THROW((void)parse(environment));
        }
        for (const auto value : {0ULL, static_cast<unsigned long long>(std::numeric_limits<unsigned int>::max()) + 1})
        {
            environment["ORM_CXX_DATABASE_CONNECT_TIMEOUT_SECONDS"] = std::to_string(value);
            EXPECT_THROW((void)parse(environment), std::invalid_argument);
        }
    }
    auto environment = sqlite();
    for (const auto value : {0LL, 1LL, static_cast<long long>(std::numeric_limits<int>::max())})
    {
        environment["ORM_CXX_DATABASE_BUSY_TIMEOUT_MS"] = std::to_string(value);
        EXPECT_EQ(std::get<orm::SqliteConfig>(parse(environment)).busyTimeout->count(), value);
    }
    for (const auto* bad : {"", "-1", "2x", "2147483648"})
    {
        environment["ORM_CXX_DATABASE_BUSY_TIMEOUT_MS"] = bad;
        EXPECT_THROW((void)parse(environment), std::invalid_argument);
    }
}

TEST(MigrationConnectionEnvironmentTest, rejectsFieldsFromOtherBackendsEvenWhenEmpty)
{
    for (const auto* backend : {"sqlite", "postgresql", "mysql", "mariadb"})
    {
        Environment environment{{"ORM_CXX_DATABASE_BACKEND", backend}, {"ORM_CXX_DATABASE_NAME", "database"}};
        const std::vector<std::string> wrong =
            std::string_view{backend} == "sqlite" ?
                std::vector<std::string>{
                    "HOST",     "PORT",   "USER",     "PASSWORD", "SOCKET", "PASSFILE", "CONNECT_TIMEOUT_SECONDS",
                    "SSL_MODE", "SSL_CA", "SSL_CERT", "SSL_KEY"} :
                std::vector<std::string>{"BUSY_TIMEOUT_MS", "SQLITE_OPEN_MODE",
                                         std::string_view{backend} == "postgresql" ? "SOCKET" : "PASSFILE"};
        for (const auto& field : wrong)
        {
            auto invalid = environment;
            invalid["ORM_CXX_DATABASE_" + field] = "";
            EXPECT_THROW((void)parse(invalid), std::invalid_argument);
        }
    }
}

TEST(MigrationConnectionEnvironmentTest, mapsTlsModesPathsAndSqliteOpenModes)
{
    for (const auto* backend : {"postgresql", "mysql", "mariadb"})
    {
        const auto postgres = std::string_view{backend} == "postgresql";
        const auto modes =
            postgres ? std::vector<std::string>{"disable", "allow", "prefer", "require", "verify-ca", "verify-full"} :
                       std::vector<std::string>{"disabled", "preferred", "required", "verify-ca", "verify-identity"};
        Environment environment{{"ORM_CXX_DATABASE_BACKEND", backend},
                                {"ORM_CXX_DATABASE_SSL_CA", ""},
                                {"ORM_CXX_DATABASE_SSL_CERT", "certificate = 'file'"},
                                {"ORM_CXX_DATABASE_SSL_KEY", "key\\file"}};
        for (std::size_t index = 0; index < modes.size(); ++index)
        {
            environment["ORM_CXX_DATABASE_SSL_MODE"] = modes[index];
            std::visit(
                [&](const auto& config)
                {
                    using Config = std::decay_t<decltype(config)>;
                    if constexpr (!std::is_same_v<Config, orm::SqliteConfig>)
                    {
                        EXPECT_EQ(static_cast<std::size_t>(*config.tls.mode), index);
                        ASSERT_TRUE(config.tls.caCertificate);
                        EXPECT_TRUE(config.tls.caCertificate->empty());
                        EXPECT_TRUE(config.tls.clientCertificate == std::filesystem::path{"certificate = 'file'"});
                        EXPECT_TRUE(config.tls.clientKey == std::filesystem::path{"key\\file"});
                    }
                },
                parse(environment));
        }
        for (const auto* bad : {"", "unknown", "REQUIRE"})
        {
            environment["ORM_CXX_DATABASE_SSL_MODE"] = bad;
            EXPECT_THROW((void)parse(environment), std::invalid_argument);
        }
    }
    auto environment = sqlite();
    for (const auto* mode : {"read-write-create", "read-write", "read-only"})
    {
        environment["ORM_CXX_DATABASE_SQLITE_OPEN_MODE"] = mode;
        EXPECT_NO_THROW((void)parse(environment));
    }
    environment["ORM_CXX_DATABASE_SQLITE_OPEN_MODE"] = "";
    EXPECT_THROW((void)parse(environment), std::invalid_argument);
}

TEST(MigrationConnectionEnvironmentTest, rejectsNulAndNeverExposesSuppliedValues)
{
    const std::vector<Environment> invalid{
        {{"ORM_CXX_DATABASE_BACKEND", "secret-backend"}},
        {{"ORM_CXX_DATABASE_BACKEND", std::string{"postgresql\0secret", 17}}},
        {{"ORM_CXX_DATABASE_BACKEND", "postgresql"}, {"UNRELATED_SECRET", "secret-value"}},
        {{"ORM_CXX_DATABASE_BACKEND", "postgresql"}, {"ORM_CXX_DATABASE_PASSWORD", std::string{"secret\0value", 12}}},
        {{"ORM_CXX_DATABASE_BACKEND", "postgresql"}, {"ORM_CXX_DATABASE_PORT", "secret-port"}}};
    for (const auto& environment : invalid)
    {
        try
        {
            (void)parse(environment);
            FAIL();
        }
        catch (const std::invalid_argument& error)
        {
            EXPECT_TRUE(std::string_view{error.what()}.find("secret") == std::string_view::npos);
        }
    }
}
