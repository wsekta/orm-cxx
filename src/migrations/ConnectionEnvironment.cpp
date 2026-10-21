module;
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
module orm;
import :migrations_internal;

namespace orm::migrations::detail
{
namespace
{
constexpr std::string_view prefix = "ORM_CXX_DATABASE_";
constexpr std::array fields{"BACKEND",
                            "NAME",
                            "HOST",
                            "PORT",
                            "USER",
                            "PASSWORD",
                            "SOCKET",
                            "CONNECT_TIMEOUT_SECONDS",
                            "BUSY_TIMEOUT_MS",
                            "PASSFILE",
                            "SSL_MODE",
                            "SSL_CA",
                            "SSL_CERT",
                            "SSL_KEY",
                            "SQLITE_OPEN_MODE"};
[[noreturn]] auto invalid() -> void
{
    throw std::invalid_argument{"Invalid migration database configuration"};
}
auto number(const std::string& text, unsigned long long maximum) -> unsigned long long
{
    unsigned long long result{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || result > maximum)
        invalid();
    return result;
}
}
auto connectionEnvironmentFromProcess() -> ConnectionEnvironment
{
    ConnectionEnvironment result;
    for (const auto* field : fields)
    {
        const auto name = std::string{prefix} + field;
        if (const auto* value = std::getenv(name.c_str()))
            result.emplace(name, value);
    }
    return result;
}
auto connectionConfiguration(const ConnectionEnvironment& environment) -> ConnectionConfiguration
{
    const auto field = [&](std::string_view name) -> std::optional<std::string>
    {
        const auto found = environment.find(std::string{prefix} + std::string{name});
        if (found == environment.end())
            return std::nullopt;
        if (found->second.find('\0') != std::string::npos)
            invalid();
        return found->second;
    };
    const auto backend = field("BACKEND");
    if (!backend || (*backend != "sqlite" && *backend != "postgresql" && *backend != "mysql" && *backend != "mariadb"))
        invalid();
    for (const auto& [name, value] : environment)
    {
        if (!name.starts_with(prefix))
            invalid();
        const auto key = std::string_view{name}.substr(prefix.size());
        bool allowed = key == "BACKEND" || key == "NAME";
        if (*backend == "sqlite")
            allowed = allowed || key == "BUSY_TIMEOUT_MS" || key == "SQLITE_OPEN_MODE";
        else
        {
            allowed = allowed || key == "HOST" || key == "PORT" || key == "USER" || key == "PASSWORD" ||
                      key == "CONNECT_TIMEOUT_SECONDS" || key == "SSL_MODE" || key == "SSL_CA" || key == "SSL_CERT" ||
                      key == "SSL_KEY";
            allowed = allowed || (*backend == "postgresql" ? key == "PASSFILE" : key == "SOCKET");
        }
        if (!allowed || value.find('\0') != std::string::npos)
            invalid();
    }
    const auto path = [&](std::string_view name) -> std::optional<std::filesystem::path>
    {
        const auto value = field(name);
        if (!value)
            return std::nullopt;
        return std::filesystem::path{std::u8string{reinterpret_cast<const char8_t*>(value->data()), value->size()}};
    };
    if (*backend == "sqlite")
    {
        const auto name = field("NAME");
        if (!name || name->empty())
            invalid();
        SqliteConfig config;
        if (*name == ":memory:")
            config.database = SqliteMemory{};
        else
            config.database =
                std::filesystem::path{std::u8string{reinterpret_cast<const char8_t*>(name->data()), name->size()}};
        if (const auto timeout = field("BUSY_TIMEOUT_MS"))
            config.busyTimeout = std::chrono::milliseconds{
                static_cast<std::chrono::milliseconds::rep>(number(*timeout, std::numeric_limits<int>::max()))};
        if (const auto mode = field("SQLITE_OPEN_MODE"))
        {
            if (*mode == "read-write-create")
                config.openMode = SqliteOpenMode::ReadWriteCreate;
            else if (*mode == "read-write")
                config.openMode = SqliteOpenMode::ReadWrite;
            else if (*mode == "read-only")
                config.openMode = SqliteOpenMode::ReadOnly;
            else
                invalid();
        }
        return config;
    }
    const auto configure = [&]<class Config>(Config config) -> ConnectionConfiguration
    {
        config.host = field("HOST");
        config.database = field("NAME");
        config.user = field("USER");
        config.password = field("PASSWORD");
        if (const auto port = field("PORT"))
            config.port = static_cast<std::uint16_t>(number(*port, 65535));
        if (const auto timeout = field("CONNECT_TIMEOUT_SECONDS"))
        {
            constexpr auto postgres = std::is_same_v<Config, PostgresConfig>;
            const auto seconds =
                number(*timeout, postgres ? static_cast<unsigned long long>(std::numeric_limits<int>::max()) :
                                            static_cast<unsigned long long>(std::numeric_limits<unsigned int>::max()));
            if (!postgres && seconds == 0)
                invalid();
            config.connectTimeout = std::chrono::seconds{static_cast<std::chrono::seconds::rep>(seconds)};
        }
        config.tls.caCertificate = path("SSL_CA");
        config.tls.clientCertificate = path("SSL_CERT");
        config.tls.clientKey = path("SSL_KEY");
        if constexpr (std::is_same_v<Config, PostgresConfig>)
        {
            config.passfile = path("PASSFILE");
            if (const auto mode = field("SSL_MODE"))
            {
                if (*mode == "disable")
                    config.tls.mode = PostgresSslMode::Disable;
                else if (*mode == "allow")
                    config.tls.mode = PostgresSslMode::Allow;
                else if (*mode == "prefer")
                    config.tls.mode = PostgresSslMode::Prefer;
                else if (*mode == "require")
                    config.tls.mode = PostgresSslMode::Require;
                else if (*mode == "verify-ca")
                    config.tls.mode = PostgresSslMode::VerifyCa;
                else if (*mode == "verify-full")
                    config.tls.mode = PostgresSslMode::VerifyFull;
                else
                    invalid();
            }
        }
        else
        {
            config.socket = path("SOCKET");
            if (const auto mode = field("SSL_MODE"))
            {
                if (*mode == "disabled")
                    config.tls.mode = MysqlSslMode::Disabled;
                else if (*mode == "preferred")
                    config.tls.mode = MysqlSslMode::Preferred;
                else if (*mode == "required")
                    config.tls.mode = MysqlSslMode::Required;
                else if (*mode == "verify-ca")
                    config.tls.mode = MysqlSslMode::VerifyCa;
                else if (*mode == "verify-identity")
                    config.tls.mode = MysqlSslMode::VerifyIdentity;
                else
                    invalid();
            }
        }
        return config;
    };
    if (*backend == "postgresql")
        return configure(PostgresConfig{});
    if (*backend == "mysql")
        return configure(MysqlConfig{});
    return configure(MariadbConfig{});
}
}
