module;
#include <filesystem>
#include <libpq-fe.h>

#include "src/database/postgresql/ConnectionParameters.hpp"
#include "tests/UnitTestPrelude.hpp"
module orm;
import :internal;
import :connection_config;
import :sql;

TEST(PostgresConnectionTransportTest, sociSerializerAndLibpqPreserveEveryByte)
{
    const std::vector<std::string> values{"",
                                          "ascii",
                                          "a b",
                                          "a'b",
                                          "a\"b",
                                          "a\\b",
                                          "a\\",
                                          "a\\'b",
                                          "a\tb",
                                          "a\rb",
                                          "a\nb",
                                          "a\vb",
                                          "a\fb",
                                          "a=b",
                                          "\xc5\xbc\xc3\xb3\xc5\x82\xc4\x87",
                                          " \\ '\t\r\n\v\f = \" "};
    for (const auto& value : values)
    {
        soci::connection_parameters parameters;
        parameters.set_option("password", orm_postgresql_connection_detail::prepareValue(value));
        parameters.extract_options_from_space_separated_string();
        const auto serialized = parameters.build_string_from_options('\'');
        char* error = nullptr;
        auto* parsed = PQconninfoParse(serialized.c_str(), &error);
        PQfreemem(error);
        ASSERT_NE(parsed, nullptr);
        bool found = false;
        for (auto* option = parsed; option->keyword; ++option)
            if (std::string_view{option->keyword} == "password")
                found = option->val && value == option->val;
        PQconninfoFree(parsed);
        EXPECT_TRUE(found);
    }
    EXPECT_THROW(orm_postgresql_connection_detail::prepareValue(std::string{"secret\0value", 12}),
                 std::invalid_argument);
}

TEST(PostgresConnectionTransportTest, rejectsInvalidFieldsBeforeOpeningWithoutValues)
{
    const std::string nul{"secret\0value", 12};
    std::vector<orm::PostgresConfig> configs{
        {.host = nul},
        {.database = nul},
        {.user = nul},
        {.password = nul},
        {.connectTimeout = std::chrono::seconds{-1}},
        {.connectTimeout = std::chrono::seconds{static_cast<long long>(std::numeric_limits<int>::max()) + 1}},
        {.passfile = std::filesystem::path{nul}},
        // Deliberately invalid input must be rejected before opening a client connection.
        // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
        {.tls = {.mode = static_cast<orm::PostgresSslMode>(99)}},
        {.tls = {.caCertificate = std::filesystem::path{nul}}},
        {.tls = {.clientCertificate = std::filesystem::path{nul}}},
        {.tls = {.clientKey = std::filesystem::path{nul}}}};
    const orm::db::postgresql::PostgresqlBackend backend;
    for (const auto& config : configs)
    {
        soci::session session;
        try
        {
            backend.runtime().open(session, orm::db::ConnectionConfigView{config});
            FAIL();
        }
        catch (const orm::DatabaseError& error)
        {
            EXPECT_EQ(error.getCode(), orm::DatabaseErrorCode::Connection);
            EXPECT_EQ(error.getBackendType(), orm::db::BackendType::Postgres);
            EXPECT_EQ(error.getOperation(), "connect");
            EXPECT_TRUE(std::string_view{error.what()}.find("secret") == std::string_view::npos);
        }
        EXPECT_FALSE(session.is_connected());
    }
}
