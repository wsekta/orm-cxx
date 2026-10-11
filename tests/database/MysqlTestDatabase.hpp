#pragma once

#include "tests/database/postgresql/PostgresqlTestSchema.hpp"

namespace mysql_test
{
inline auto configuredDsn(orm::db::BackendType type) -> std::string
{
    const auto* value =
        std::getenv(type == orm::db::BackendType::Mysql ? "ORM_CXX_MYSQL_TEST_DSN" : "ORM_CXX_MARIADB_TEST_DSN");
    return value == nullptr ? std::string{} : std::string{value};
}

// Preserve quoted client options, replacing only the database selection.
inline auto isolatedDsn(std::string_view dsn, std::string_view database) -> std::string
{
    if (not postgresql_test::isSafeSchemaName(database))
        throw std::invalid_argument{"Unsafe MySQL family test database name"};
    const auto prefix = dsn.find("://");
    if (prefix == std::string_view::npos)
        throw std::invalid_argument{"Expected a backend keyword/value DSN"};
    std::string result{dsn.substr(0, prefix + 3)};
    auto position = prefix + 3;
    while (position < dsn.size())
    {
        while (position < dsn.size() and std::isspace(static_cast<unsigned char>(dsn[position])))
            ++position;
        const auto start = position;
        char quote = 0;
        while (position < dsn.size())
        {
            const auto character = dsn[position];
            if (character == '\\' and position + 1 < dsn.size())
            {
                position += 2;
                continue;
            }
            if (quote != 0)
            {
                if (character == quote)
                    quote = 0;
            }
            else if (character == '\'' or character == '"')
                quote = character;
            else if (std::isspace(static_cast<unsigned char>(character)))
                break;
            ++position;
        }
        if (quote != 0)
            throw std::invalid_argument{"Unterminated DSN option"};
        const auto token = dsn.substr(start, position - start);
        if (not token.empty() and not token.starts_with("db="))
        {
            result.append(token);
            result.push_back(' ');
        }
    }
    result += "db=";
    result += database;
    return result;
}

class MysqlTestDatabase
{
public:
    MysqlTestDatabase(orm::db::BackendType type, const std::string& dsn)
        : name{postgresql_test::generateSchemaName()}, isolatedConnection{isolatedDsn(dsn, name)}
    {
        orm::db::CommandGeneratorFactory factory;
        factory.getBackend(type).runtime().open(adminSession, dsn);
        adminSession << "CREATE DATABASE `" + name + "`";
        active = true;
    }
    MysqlTestDatabase(const MysqlTestDatabase&) = delete;
    auto operator=(const MysqlTestDatabase&) -> MysqlTestDatabase& = delete;
    ~MysqlTestDatabase()
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
        return isolatedConnection;
    }
    auto drop() -> void
    {
        if (not active)
            return;
        if (not postgresql_test::isSafeSchemaName(name))
            throw std::logic_error{"Unsafe test database cleanup"};
        adminSession << "DROP DATABASE `" + name + "`";
        active = false;
        adminSession.close();
    }

private:
    std::string name;
    std::string isolatedConnection;
    soci::session adminSession;
    bool active = false;
};
}
