module;

#include <chrono>
#include <concepts>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <variant>

export module orm:connection_config;

import :foundation_types;

export namespace orm
{
struct SqliteMemory
{
};
enum class SqliteOpenMode
{
    ReadWriteCreate,
    ReadWrite,
    ReadOnly
};

struct SqliteConfig
{
    std::variant<std::filesystem::path, SqliteMemory> database{};
    std::optional<SqliteOpenMode> openMode{};
    std::optional<std::chrono::milliseconds> busyTimeout{};
};

enum class PostgresSslMode
{
    Disable,
    Allow,
    Prefer,
    Require,
    VerifyCa,
    VerifyFull
};
struct PostgresTls
{
    std::optional<PostgresSslMode> mode{};
    std::optional<std::filesystem::path> caCertificate{};
    std::optional<std::filesystem::path> clientCertificate{};
    std::optional<std::filesystem::path> clientKey{};
};

enum class MysqlSslMode
{
    Disabled,
    Preferred,
    Required,
    VerifyCa,
    VerifyIdentity
};
struct MysqlTls
{
    std::optional<MysqlSslMode> mode{};
    std::optional<std::filesystem::path> caCertificate{};
    std::optional<std::filesystem::path> clientCertificate{};
    std::optional<std::filesystem::path> clientKey{};
};

struct PostgresConfig
{
    std::optional<std::string> host{};
    std::optional<std::uint16_t> port{};
    std::optional<std::string> database{};
    std::optional<std::string> user{};
    std::optional<std::string> password{};
    std::optional<std::chrono::seconds> connectTimeout{};
    std::optional<std::filesystem::path> passfile{};
    PostgresTls tls{};
};

struct MysqlConfig
{
    std::optional<std::string> host{};
    std::optional<std::uint16_t> port{};
    std::optional<std::string> database{};
    std::optional<std::string> user{};
    std::optional<std::string> password{};
    std::optional<std::filesystem::path> socket{};
    std::optional<std::chrono::seconds> connectTimeout{};
    MysqlTls tls{};
};

struct MariadbConfig
{
    std::optional<std::string> host{};
    std::optional<std::uint16_t> port{};
    std::optional<std::string> database{};
    std::optional<std::string> user{};
    std::optional<std::string> password{};
    std::optional<std::filesystem::path> socket{};
    std::optional<std::chrono::seconds> connectTimeout{};
    MysqlTls tls{};
};
}

export namespace orm::db
{
template <class Config>
struct ConnectionTraits;

template <>
struct ConnectionTraits<SqliteConfig>
{
    static constexpr auto backendType = BackendType::Sqlite;
};
template <>
struct ConnectionTraits<PostgresConfig>
{
    static constexpr auto backendType = BackendType::Postgres;
};
template <>
struct ConnectionTraits<MysqlConfig>
{
    static constexpr auto backendType = BackendType::Mysql;
};
template <>
struct ConnectionTraits<MariadbConfig>
{
    static constexpr auto backendType = BackendType::Mariadb;
};

template <class Config>
concept ConnectionConfiguration =
    std::same_as<Config, std::remove_cvref_t<Config>> && std::is_object_v<Config> && requires {
        { ConnectionTraits<Config>::backendType } -> std::same_as<const BackendType&>;
        typename std::integral_constant<BackendType, ConnectionTraits<Config>::backendType>;
    };

/** Borrows an lvalue only during synchronous BackendRuntime::open; never cache it. */
class ConnectionConfigView final
{
public:
    template <ConnectionConfiguration Config>
    explicit ConnectionConfigView(const Config& config) noexcept
        : value{std::addressof(config)}, type{&typeid(Config)}, backend{ConnectionTraits<Config>::backendType}
    {
    }

    template <ConnectionConfiguration Config>
    ConnectionConfigView(const Config&&) = delete;

    [[nodiscard]] auto backendType() const noexcept -> BackendType
    {
        return backend;
    }

    template <ConnectionConfiguration Config>
    [[nodiscard]] auto get() const -> const Config&
    {
        if (*type != typeid(Config))
        {
            throw DatabaseError{DatabaseErrorCode::Connection, backend, "connect",
                                "Connection configuration type mismatch"};
        }
        return *static_cast<const Config*>(value);
    }

private:
    const void* value;
    const std::type_info* type;
    BackendType backend;
};
}
