#pragma once

inline auto makeExampleDatabase(int argc, char** argv) -> orm::Database
{
    if (argc != 3)
        throw std::invalid_argument{"Usage: example <sqlite|postgresql|mysql|mariadb> <database-or-file>"};
    const auto backend = std::string_view{argv[1]};
    const auto database = std::string{argv[2]};
    if (backend == "sqlite")
    {
        if (database == ":memory:")
            return orm::Database{orm::SqliteConfig{.database = orm::SqliteMemory{}}};
        return orm::Database{
            orm::SqliteConfig{.database = std::filesystem::path{std::u8string{database.begin(), database.end()}}}};
    }
    if (backend == "postgresql")
        return orm::Database{orm::PostgresConfig{.database = database}};
    if (backend == "mysql")
        return orm::Database{orm::MysqlConfig{.database = database}};
    if (backend == "mariadb")
        return orm::Database{orm::MariadbConfig{.database = database}};
    throw std::invalid_argument{"Unknown example backend"};
}
