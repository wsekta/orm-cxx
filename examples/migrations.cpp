#include <iostream>
#include <string>

import orm;

int main(int argc, char** argv)
{
    using namespace orm::migrations;
    const SqlMigration initial{"CREATE TABLE migration_example_users (id INTEGER PRIMARY KEY, name TEXT NOT NULL);"
                               "INSERT INTO migration_example_users VALUES (1, 'Ann');",
                               "DROP TABLE migration_example_users;"};
    const SqlMigration email{"ALTER TABLE migration_example_users ADD COLUMN email TEXT;"
                             "UPDATE migration_example_users SET email='ann@example.com';",
                             "ALTER TABLE migration_example_users DROP COLUMN email;"};
    const Catalog catalog{
        Migration{1, "initial", {{orm::db::BackendType::Sqlite, initial}, {orm::db::BackendType::Postgres, initial}}},
        Migration{2, "email", {{orm::db::BackendType::Sqlite, email}, {orm::db::BackendType::Postgres, email}}}};
    return runCommandLine(argc, argv, catalog);
}
