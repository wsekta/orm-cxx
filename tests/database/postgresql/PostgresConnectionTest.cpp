module;
#include <filesystem>
#include <fstream>
#include <libpq-fe.h>

#include "soci/postgresql/soci-postgresql.h"
#include "tests/UnitTestPrelude.hpp"
module orm;
import :internal;
import :connection_config;
import :test_support;
import :sql;
import :database;

namespace orm::test
{
auto setConnectionEnvironment(const char* key, const std::optional<std::string>& value) -> void;
}

namespace
{
auto pgHandle(soci::session& session) -> PGconn*
{
    return static_cast<soci::postgresql_session_backend*>(session.get_backend())->conn_;
}
auto openPostgres(soci::session& session, const orm::PostgresConfig& config) -> void
{
    const orm::db::postgresql::PostgresqlBackend backend;
    backend.runtime().open(session, orm::db::ConnectionConfigView{config});
    backend.runtime().onConnect(session);
}
auto connectionOption(soci::session& session, std::string_view name) -> std::string
{
    auto* options = PQconninfo(pgHandle(session));
    if (!options)
        throw std::runtime_error{"Cannot inspect PostgreSQL connection"};
    std::string result;
    for (auto* option = options; option->keyword; ++option)
        if (name == option->keyword && option->val)
            result = option->val;
    PQconninfoFree(options);
    return result;
}
auto sqlLiteral(soci::session& session, std::string_view value) -> std::string
{
    auto* escaped = PQescapeLiteral(pgHandle(session), value.data(), value.size());
    if (!escaped)
        throw std::runtime_error{"Cannot quote PostgreSQL test value"};
    std::string result{escaped};
    PQfreemem(escaped);
    return result;
}
auto sqlIdentifier(soci::session& session, std::string_view value) -> std::string
{
    auto* escaped = PQescapeIdentifier(pgHandle(session), value.data(), value.size());
    if (!escaped)
        throw std::runtime_error{"Cannot quote PostgreSQL test identifier"};
    std::string result{escaped};
    PQfreemem(escaped);
    return result;
}
class PgEnvironment
{
public:
    auto set(const char* key, const std::optional<std::string>& value) -> void
    {
        const auto* previous = std::getenv(key);
        saved.try_emplace(key, previous ? std::optional<std::string>{previous} : std::nullopt);
        assign(key, value);
    }
    ~PgEnvironment()
    {
        for (const auto& [key, value] : saved)
            assign(key.c_str(), value);
    }

private:
    static auto assign(const char* key, const std::optional<std::string>& value) -> void
    {
        orm::test::setConnectionEnvironment(key, value);
    }
    std::map<std::string, std::optional<std::string>> saved;
};
class PgLogin
{
public:
    PgLogin()
        : base{orm::test::postgresConfigFromEnvironment()},
          role{"orm_cxx_login_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
               " = '\\\t\xc5\xbc"}
    {
        openPostgres(admin, base);
        admin << "CREATE ROLE " + sqlIdentifier(admin, role) + " LOGIN";
    }
    ~PgLogin()
    {
        try
        {
            admin << "DROP ROLE " + sqlIdentifier(admin, role);
        }
        catch (...)
        {
        }
    }
    auto config(std::string password) -> orm::PostgresConfig
    {
        auto result = base;
        result.user = role;
        result.password = std::move(password);
        result.passfile = std::filesystem::path{};
        return result;
    }
    auto password(std::string_view value) -> void
    {
        admin << "ALTER ROLE " + sqlIdentifier(admin, role) + " PASSWORD " + sqlLiteral(admin, value);
    }
    orm::PostgresConfig base;
    std::string role;
    soci::session admin;
};
class FailingPgInitialization final : public postgresql_test::SchemaRuntime
{
public:
    explicit FailingPgInitialization(const orm::db::BackendRuntime& delegate)
        : SchemaRuntime{delegate, postgresql_test::generateSchemaName()}
    {
    }
    auto onConnect(soci::session& session) const -> void override
    {
        session << "SELECT 1/0";
    }
};
class FailingPgBackend final : public orm::db::BackendProvider
{
public:
    FailingPgBackend() : initialization{backend.runtime()} {}
    auto type() const noexcept -> orm::db::BackendType override
    {
        return backend.type();
    }
    auto capabilities() const noexcept -> const orm::db::BackendCapabilities& override
    {
        return backend.capabilities();
    }
    auto dialect() const noexcept -> const orm::db::SqlDialect& override
    {
        return backend.dialect();
    }
    auto runtime() const noexcept -> const orm::db::BackendRuntime& override
    {
        return initialization;
    }
    auto commandGenerator() const noexcept -> const orm::db::CommandGenerator& override
    {
        return backend.commandGenerator();
    }

private:
    orm::db::postgresql::PostgresqlBackend backend;
    FailingPgInitialization initialization;
};
struct PgPassfile
{
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("orm_cxx_pass_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~PgPassfile()
    {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
    static auto escape(std::string_view text) -> std::string
    {
        std::string result;
        for (const auto value : text)
        {
            if (value == ':' || value == '\\')
                result.push_back('\\');
            result.push_back(value);
        }
        return result;
    }
    auto write(std::string_view host, std::string_view port, std::string_view database, std::string_view user,
               std::string_view password) -> void
    {
        std::ofstream file{path, std::ios::binary};
        file << escape(host) << ':' << escape(port) << ':' << escape(database) << ':' << escape(user) << ':'
             << escape(password) << '\n';
        file.close();
        std::filesystem::permissions(path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                     std::filesystem::perm_options::replace);
    }
};
}

TEST(PostgresConnectionTest, authenticatesWithDifficultPasswordsAndSanitizesFailure)
{
    PgLogin login;
    const std::vector<std::string> passwords{"a b",
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
    for (const auto& password : passwords)
    {
        login.password(password);
        soci::session session;
        openPostgres(session, login.config(password));
        ASSERT_EQ(PQconnectionUsedPassword(pgHandle(session)), 1)
            << "Authentication test requires password authentication";
        EXPECT_TRUE(connectionOption(session, "password") == password);
        std::string user;
        session << "SELECT current_user", soci::into(user);
        EXPECT_EQ(user, login.role);
    }
    const auto secret = std::string{"wrong-secret-do-not-log"};
    try
    {
        orm::Database failed{login.config(secret)};
        FAIL();
    }
    catch (const orm::DatabaseError& error)
    {
        EXPECT_EQ(error.getCode(), orm::DatabaseErrorCode::Connection);
        EXPECT_EQ(error.getOperation(), "connect");
        EXPECT_TRUE(std::string_view{error.what()}.find(secret) == std::string_view::npos);
        EXPECT_TRUE(std::string_view{error.what()}.find(login.role) == std::string_view::npos);
    }
}

TEST(PostgresConnectionTest, nativeEnvironmentAndPassfileDefaultsRemainAvailable)
{
    PgLogin login;
    const std::string password{"default p'ass\\word=42"};
    login.password(password);
    PgEnvironment environment;
    environment.set("PGHOST", PQhost(pgHandle(login.admin)));
    environment.set("PGPORT", PQport(pgHandle(login.admin)));
    environment.set("PGDATABASE", PQdb(pgHandle(login.admin)));
    environment.set("PGUSER", login.role);
    environment.set("PGPASSWORD", password);
    environment.set("PGSERVICE", std::nullopt);
    environment.set("PGSERVICEFILE", std::nullopt);
    PgPassfile passfile;
    passfile.write(PQhost(pgHandle(login.admin)), PQport(pgHandle(login.admin)), PQdb(pgHandle(login.admin)),
                   login.role, password);
    environment.set("PGPASSFILE", passfile.path.string());
    for (const auto& config : {orm::PostgresConfig{}, orm::PostgresConfig{.password = std::string{}}})
    {
        soci::session session;
        openPostgres(session, config);
        ASSERT_EQ(PQconnectionUsedPassword(pgHandle(session)), 1);
        EXPECT_TRUE(std::string_view{PQuser(pgHandle(session))} == login.role);
    }
    environment.set("PGPASSWORD", std::nullopt);
    for (const auto& config : {orm::PostgresConfig{}, orm::PostgresConfig{.password = std::string{}},
                               orm::PostgresConfig{.passfile = passfile.path}})
    {
        soci::session session;
        openPostgres(session, config);
        EXPECT_EQ(PQconnectionUsedPassword(pgHandle(session)), 1);
    }
    auto explicitConfig = login.config(password);
    explicitConfig.connectTimeout = std::chrono::seconds{0};
    soci::session explicitSession;
    openPostgres(explicitSession, explicitConfig);
    EXPECT_TRUE(connectionOption(explicitSession, "connect_timeout") == "0");
    auto maximumTimeout = explicitConfig;
    maximumTimeout.connectTimeout = std::chrono::seconds{std::numeric_limits<int>::max()};
    soci::session maximum;
    openPostgres(maximum, maximumTimeout);
    EXPECT_TRUE(connectionOption(maximum, "connect_timeout") == std::to_string(std::numeric_limits<int>::max()));
    environment.set("PGPASSWORD", "invalid-environment-password");
    soci::session explicitOverride;
    openPostgres(explicitOverride, explicitConfig);
    EXPECT_EQ(PQconnectionUsedPassword(pgHandle(explicitOverride)), 1);
}

TEST(PostgresConnectionTest, explicitTlsModesAndCertificatesReachLibpq)
{
    const auto base = orm::test::postgresConfigFromEnvironment();
    auto disabled = base;
    disabled.tls.mode = orm::PostgresSslMode::Disable;
    soci::session plain;
    openPostgres(plain, disabled);
    EXPECT_EQ(PQsslInUse(pgHandle(plain)), 0);
    for (const auto mode : {orm::PostgresSslMode::Allow, orm::PostgresSslMode::Prefer, orm::PostgresSslMode::Require})
    {
        auto config = base;
        config.tls.mode = mode;
        soci::session session;
        openPostgres(session, config);
        if (mode == orm::PostgresSslMode::Require)
            EXPECT_EQ(PQsslInUse(pgHandle(session)), 1);
    }
    // Phase 11 provisions a CA and a hostname matching the server certificate.
    ASSERT_TRUE(base.tls.caCertificate.has_value()) << "TLS verification requires ORM_CXX_POSTGRESQL_TEST_SSL_CA";
    for (const auto mode : {orm::PostgresSslMode::VerifyCa, orm::PostgresSslMode::VerifyFull})
    {
        auto config = base;
        config.tls.mode = mode;
        soci::session session;
        openPostgres(session, config);
        EXPECT_EQ(PQsslInUse(pgHandle(session)), 1);
        std::string clientIdentity;
        soci::indicator identityState{};
        session << "SELECT client_dn FROM pg_stat_ssl WHERE pid=pg_backend_pid()",
            soci::into(clientIdentity, identityState);
        ASSERT_NE(identityState, soci::i_null);
        EXPECT_NE(clientIdentity.find("CN=orm_cxx"), std::string::npos);
    }
    ASSERT_TRUE(base.tls.clientCertificate.has_value()) << "TLS client test requires ORM_CXX_POSTGRESQL_TEST_SSL_CERT";
    ASSERT_TRUE(base.tls.clientKey.has_value()) << "TLS client test requires ORM_CXX_POSTGRESQL_TEST_SSL_KEY";
    PgEnvironment environment;
    environment.set("PGHOSTADDR", PQhostaddr(pgHandle(plain)));
    auto wrongIdentity = base;
    wrongIdentity.host = "orm-cxx-wrong-host.invalid";
    wrongIdentity.tls.mode = orm::PostgresSslMode::VerifyFull;
    soci::session wrongIdentitySession;
    EXPECT_THROW(openPostgres(wrongIdentitySession, wrongIdentity), orm::DatabaseError);
    auto invalid = base;
    invalid.tls.mode = orm::PostgresSslMode::VerifyCa;
    invalid.tls.caCertificate = std::filesystem::temp_directory_path() / "orm_cxx_missing_ca.pem";
    soci::session failed;
    EXPECT_THROW(openPostgres(failed, invalid), orm::DatabaseError);
    EXPECT_FALSE(failed.is_connected());
    auto invalidClient = base;
    invalidClient.tls.mode = orm::PostgresSslMode::Require;
    PgPassfile malformedCertificate;
    {
        std::ofstream certificate{malformedCertificate.path, std::ios::binary};
        certificate << "invalid PEM certificate";
    }
    invalidClient.tls.clientCertificate = malformedCertificate.path;
    EXPECT_THROW(openPostgres(failed, invalidClient), orm::DatabaseError);
}

TEST(PostgresConnectionTest, reportsServerInitializationFailureAndCanConnectAgain)
{
    const auto config = orm::test::postgresConfigFromEnvironment();
    std::vector<std::unique_ptr<orm::db::BackendProvider>> providers;
    providers.push_back(std::make_unique<FailingPgBackend>());
    try
    {
        orm::Database failed{config, orm::db::CommandGeneratorFactory{std::move(providers)}};
        FAIL() << "Expected server initialization failure";
    }
    catch (const orm::DatabaseError& error)
    {
        EXPECT_EQ(error.getCode(), orm::DatabaseErrorCode::Connection);
        EXPECT_EQ(error.getOperation(), "connect");
        EXPECT_TRUE(std::string_view{error.what()}.find("SELECT") == std::string_view::npos);
    }
    EXPECT_NO_THROW((orm::Database{config}));
}
