#pragma once

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "soci/soci.h"

namespace postgresql_test
{
inline constexpr std::string_view connectionStringPrefix{"postgresql://"};
inline constexpr std::string_view schemaPrefix{"orm_cxx_test_"};

inline auto configuredDsn() -> std::string
{
    const auto* value = std::getenv("ORM_CXX_POSTGRESQL_TEST_DSN");
    return value == nullptr ? std::string{} : std::string{value};
}

inline auto connectionPayload(std::string_view connectionString) -> std::string
{
    if (not connectionString.starts_with(connectionStringPrefix))
    {
        throw std::invalid_argument{
            "ORM_CXX_POSTGRESQL_TEST_DSN must use the postgresql:// keyword/value connection format"};
    }

    const auto payload = connectionString.substr(connectionStringPrefix.size());
    if (payload.empty())
    {
        throw std::invalid_argument{"ORM_CXX_POSTGRESQL_TEST_DSN must contain PostgreSQL connection parameters"};
    }

    return std::string{payload};
}

inline auto isSafeSchemaName(std::string_view schemaName) -> bool
{
    if (not schemaName.starts_with(schemaPrefix) or schemaName.size() <= schemaPrefix.size() or
        schemaName.size() > 63 or schemaName == "public")
    {
        return false;
    }

    for (const auto character : schemaName)
    {
        const auto lowerCase = character >= 'a' and character <= 'z';
        const auto digit = character >= '0' and character <= '9';
        if (not lowerCase and not digit and character != '_')
        {
            return false;
        }
    }

    return true;
}

inline auto generateSchemaName() -> std::string
{
    static std::atomic<unsigned long long> counter{0};
    const auto timestamp =
        static_cast<unsigned long long>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    const auto entropy = (static_cast<unsigned long long>(std::random_device{}()) << 32U) ^
                         static_cast<unsigned long long>(std::random_device{}());

    std::ostringstream stream;
    stream << schemaPrefix << std::hex << std::setw(16) << std::setfill('0')
           << (timestamp ^ entropy ^ counter.fetch_add(1, std::memory_order_relaxed));
    return stream.str();
}

inline auto quotedSchemaName(std::string_view schemaName) -> std::string
{
    if (not isSafeSchemaName(schemaName))
    {
        throw std::invalid_argument{"Refusing to use an unsafe PostgreSQL test schema name"};
    }

    return "\"" + std::string{schemaName} + "\"";
}

class PostgresqlTestSchema
{
public:
    explicit PostgresqlTestSchema(std::string baseConnectionStringInit)
        : baseConnectionString{std::move(baseConnectionStringInit)}, schemaNameValue{generateSchemaName()}
    {
        if (not isSafeSchemaName(schemaNameValue))
        {
            throw std::logic_error{"Generated an invalid PostgreSQL test schema name"};
        }

        adminSession.open("postgresql", connectionPayload(baseConnectionString));
        adminSession << "CREATE SCHEMA " + quotedSchemaName(schemaNameValue) + ";";
        active = true;

        isolatedConnectionString = baseConnectionString;
        if (not isolatedConnectionString.empty() and isolatedConnectionString.back() != ' ')
        {
            isolatedConnectionString.push_back(' ');
        }
        isolatedConnectionString += "options=-csearch_path=" + schemaNameValue;
    }

    PostgresqlTestSchema(const PostgresqlTestSchema&) = delete;
    PostgresqlTestSchema(PostgresqlTestSchema&&) = delete;
    auto operator=(const PostgresqlTestSchema&) -> PostgresqlTestSchema& = delete;
    auto operator=(PostgresqlTestSchema&&) -> PostgresqlTestSchema& = delete;

    ~PostgresqlTestSchema()
    {
        try
        {
            drop();
        }
        catch (...)
        {
        }
    }

    [[nodiscard]] auto connectionString() const noexcept -> const std::string&
    {
        return isolatedConnectionString;
    }

    [[nodiscard]] auto baseDsn() const noexcept -> const std::string&
    {
        return baseConnectionString;
    }

    [[nodiscard]] auto schemaName() const noexcept -> const std::string&
    {
        return schemaNameValue;
    }

    auto admin() noexcept -> soci::session&
    {
        return adminSession;
    }

    auto drop() -> void
    {
        if (not active)
        {
            return;
        }

        if (not isSafeSchemaName(schemaNameValue))
        {
            throw std::logic_error{"Refusing to drop an unsafe PostgreSQL test schema"};
        }

        adminSession << "DROP SCHEMA " + quotedSchemaName(schemaNameValue) + " CASCADE;";
        active = false;
        adminSession.close();
    }

private:
    std::string baseConnectionString;
    std::string schemaNameValue;
    std::string isolatedConnectionString;
    soci::session adminSession;
    bool active = false;
};
} // namespace postgresql_test
