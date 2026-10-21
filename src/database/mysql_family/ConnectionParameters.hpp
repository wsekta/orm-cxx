#pragma once

#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace orm_mysql_connection
{
// SOCI's MySQL parser consumes single-quoted values and unescapes each backslash.
inline auto append(std::string& result, std::string_view name, std::string_view value) -> void
{
    if (value.find('\0') != std::string_view::npos)
        throw std::invalid_argument{"Invalid MySQL-family connection configuration"};
    if (!result.empty())
        result += ' ';
    result += name;
    result += "='";
    for (const auto character : value)
    {
        if (character == '\\' || character == '\'')
            result += '\\';
        result += character;
    }
    result += '\'';
}

inline auto pathText(const std::filesystem::path& path) -> std::string
{
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

template <class Config>
inline auto serialize(const Config& config) -> std::string
{
    std::string result;
    const auto text = [&result](std::string_view name, const auto& value)
    {
        if (value)
            append(result, name, *value);
    };
    const auto path = [&result](std::string_view name, const auto& value)
    {
        if (value)
            append(result, name, pathText(*value));
    };
    text("host", config.host);
    text("user", config.user);
    text("password", config.password);
    text("db", config.database);
    if (config.port)
        append(result, "port", std::to_string(*config.port));
    path("unix_socket", config.socket);
    if (config.connectTimeout)
    {
        const auto seconds = config.connectTimeout->count();
        // SOCI 4.1.4 accepts only positive unsigned-int seconds in this option.
        if (seconds <= 0 || static_cast<unsigned long long>(seconds) > std::numeric_limits<unsigned int>::max())
            throw std::invalid_argument{"Invalid MySQL-family connection timeout"};
        append(result, "connect_timeout", std::to_string(seconds));
    }
    path("sslca", config.tls.caCertificate);
    path("sslcert", config.tls.clientCertificate);
    path("sslkey", config.tls.clientKey);
    if (config.tls.mode)
    {
        std::string_view mode;
        using Mode = std::remove_cvref_t<decltype(*config.tls.mode)>;
        switch (*config.tls.mode)
        {
        case Mode::Disabled:
            mode = "DISABLED";
            break;
        case Mode::Preferred:
            mode = "PREFERRED";
            break;
        case Mode::Required:
            mode = "REQUIRED";
            break;
        case Mode::VerifyCa:
            mode = "VERIFY_CA";
            break;
        case Mode::VerifyIdentity:
            mode = "VERIFY_IDENTITY";
            break;
        default:
            throw std::invalid_argument{"Invalid MySQL-family TLS mode"};
        }
        append(result, "ssl_mode", mode);
    }
    return result;
}
}
