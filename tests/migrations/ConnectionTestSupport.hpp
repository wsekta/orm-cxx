#pragma once
namespace migration_connection_test
{
inline auto environment(const orm::migrations::detail::ConnectionConfiguration& value)
    -> orm::migrations::detail::ConnectionEnvironment
{
    return std::visit(
        [](const auto& config)
        {
            using Config = std::decay_t<decltype(config)>;
            orm::migrations::detail::ConnectionEnvironment result;
            const auto add = [&](std::string_view name, const auto& optional)
            {
                if (optional)
                    result.emplace("ORM_CXX_DATABASE_" + std::string{name}, *optional);
            };
            const auto pathString = [](const std::filesystem::path& path)
            {
                const auto text = path.u8string();
                return std::string{reinterpret_cast<const char*>(text.data()), text.size()};
            };
            const auto path = [&](std::string_view name, const auto& optional)
            {
                if (optional)
                    add(name, std::optional<std::string>{pathString(*optional)});
            };
            if constexpr (std::is_same_v<Config, orm::SqliteConfig>)
            {
                result["ORM_CXX_DATABASE_BACKEND"] = "sqlite";
                if (const auto* file = std::get_if<std::filesystem::path>(&config.database))
                    result["ORM_CXX_DATABASE_NAME"] = pathString(*file);
                else
                    result["ORM_CXX_DATABASE_NAME"] = ":memory:";
            }
            else
            {
                if constexpr (std::is_same_v<Config, orm::PostgresConfig>)
                    result["ORM_CXX_DATABASE_BACKEND"] = "postgresql";
                else if constexpr (std::is_same_v<Config, orm::MysqlConfig>)
                    result["ORM_CXX_DATABASE_BACKEND"] = "mysql";
                else
                    result["ORM_CXX_DATABASE_BACKEND"] = "mariadb";
                add("NAME", config.database);
                add("HOST", config.host);
                add("USER", config.user);
                add("PASSWORD", config.password);
                if (config.port)
                    result["ORM_CXX_DATABASE_PORT"] = std::to_string(*config.port);
                if (config.connectTimeout)
                    result["ORM_CXX_DATABASE_CONNECT_TIMEOUT_SECONDS"] = std::to_string(config.connectTimeout->count());
                path("SSL_CA", config.tls.caCertificate);
                path("SSL_CERT", config.tls.clientCertificate);
                path("SSL_KEY", config.tls.clientKey);
                if constexpr (std::is_same_v<Config, orm::PostgresConfig>)
                {
                    path("PASSFILE", config.passfile);
                    if (config.tls.mode)
                    {
                        constexpr std::array modes{"disable", "allow", "prefer", "require", "verify-ca", "verify-full"};
                        result["ORM_CXX_DATABASE_SSL_MODE"] = modes.at(static_cast<std::size_t>(*config.tls.mode));
                    }
                }
                else
                {
                    path("SOCKET", config.socket);
                    if (config.tls.mode)
                    {
                        constexpr std::array modes{"disabled", "preferred", "required", "verify-ca", "verify-identity"};
                        result["ORM_CXX_DATABASE_SSL_MODE"] = modes.at(static_cast<std::size_t>(*config.tls.mode));
                    }
                }
            }
            return result;
        },
        value);
}
}
