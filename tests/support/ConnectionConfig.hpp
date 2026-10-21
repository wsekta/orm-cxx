#pragma once

// Include standard-library headers before importing orm / entering module purview.
namespace orm::test
{
inline auto connectionEnvironment(std::string_view prefix, std::string_view suffix) -> std::optional<std::string>
{
    const auto key = std::string{prefix} + std::string{suffix};
    const auto* value = std::getenv(key.c_str());
    return value == nullptr ? std::nullopt : std::optional<std::string>{value};
}

inline auto connectionNumber(std::string_view prefix, std::string_view suffix) -> std::optional<unsigned long long>
{
    const auto text = connectionEnvironment(prefix, suffix);
    if (not text)
        return std::nullopt;
    unsigned long long result{};
    const auto parsed = std::from_chars(text->data(), text->data() + text->size(), result);
    if (parsed.ec != std::errc{} or parsed.ptr != text->data() + text->size())
        throw std::invalid_argument{"Invalid numeric connection test option"};
    return result;
}

inline auto connectionPath(std::string_view prefix, std::string_view suffix) -> std::optional<std::filesystem::path>
{
    const auto text = connectionEnvironment(prefix, suffix);
    if (not text)
        return std::nullopt;
    return std::filesystem::path{std::u8string{text->begin(), text->end()}};
}

template <class Config>
inline auto serverConfigFromEnvironment(std::string_view prefix) -> Config
{
    Config result{};
    result.host = connectionEnvironment(prefix, "HOST");
    result.database = connectionEnvironment(prefix, "DATABASE");
    result.user = connectionEnvironment(prefix, "USER");
    result.password = connectionEnvironment(prefix, "PASSWORD");
    if (const auto port = connectionNumber(prefix, "PORT"))
    {
        if (*port > std::numeric_limits<std::uint16_t>::max())
            throw std::invalid_argument{"Invalid connection test port"};
        result.port = static_cast<std::uint16_t>(*port);
    }
    if (const auto timeout = connectionNumber(prefix, "CONNECT_TIMEOUT_SECONDS"))
    {
        if (*timeout > static_cast<unsigned long long>(std::numeric_limits<std::chrono::seconds::rep>::max()))
            throw std::invalid_argument{"Invalid connection test timeout"};
        result.connectTimeout = std::chrono::seconds{static_cast<std::chrono::seconds::rep>(*timeout)};
    }
    result.tls.caCertificate = connectionPath(prefix, "SSL_CA");
    result.tls.clientCertificate = connectionPath(prefix, "SSL_CERT");
    result.tls.clientKey = connectionPath(prefix, "SSL_KEY");
    const auto mode = connectionEnvironment(prefix, "SSL_MODE");
    if constexpr (std::is_same_v<Config, orm::PostgresConfig>)
    {
        result.passfile = connectionPath(prefix, "PASSFILE");
        if (mode)
        {
            if (*mode == "disable")
                result.tls.mode = orm::PostgresSslMode::Disable;
            else if (*mode == "allow")
                result.tls.mode = orm::PostgresSslMode::Allow;
            else if (*mode == "prefer")
                result.tls.mode = orm::PostgresSslMode::Prefer;
            else if (*mode == "require")
                result.tls.mode = orm::PostgresSslMode::Require;
            else if (*mode == "verify-ca")
                result.tls.mode = orm::PostgresSslMode::VerifyCa;
            else if (*mode == "verify-full")
                result.tls.mode = orm::PostgresSslMode::VerifyFull;
            else
                throw std::invalid_argument{"Invalid connection test SSL mode"};
        }
    }
    else
    {
        result.socket = connectionPath(prefix, "SOCKET");
        if (mode)
        {
            if (*mode == "disabled")
                result.tls.mode = orm::MysqlSslMode::Disabled;
            else if (*mode == "preferred")
                result.tls.mode = orm::MysqlSslMode::Preferred;
            else if (*mode == "required")
                result.tls.mode = orm::MysqlSslMode::Required;
            else if (*mode == "verify-ca")
                result.tls.mode = orm::MysqlSslMode::VerifyCa;
            else if (*mode == "verify-identity")
                result.tls.mode = orm::MysqlSslMode::VerifyIdentity;
            else
                throw std::invalid_argument{"Invalid connection test SSL mode"};
        }
    }
    return result;
}
inline auto postgresConfigFromEnvironment() -> orm::PostgresConfig
{
    return serverConfigFromEnvironment<orm::PostgresConfig>("ORM_CXX_POSTGRESQL_TEST_");
}
inline auto mysqlConfigFromEnvironment() -> orm::MysqlConfig
{
    return serverConfigFromEnvironment<orm::MysqlConfig>("ORM_CXX_MYSQL_TEST_");
}
inline auto mariadbConfigFromEnvironment() -> orm::MariadbConfig
{
    return serverConfigFromEnvironment<orm::MariadbConfig>("ORM_CXX_MARIADB_TEST_");
}
}
