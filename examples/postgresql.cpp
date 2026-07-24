#include <cstdlib>
#include <iostream>
#include <string_view>

#include "orm-cxx/orm.hpp"

int main() // NOLINT(bugprone-exception-escape)
{
    const auto* connectionString = std::getenv("ORM_CXX_POSTGRESQL_EXAMPLE_DSN");

    if (connectionString == nullptr or std::string_view{connectionString}.empty())
    {
        std::cerr << "Set ORM_CXX_POSTGRESQL_EXAMPLE_DSN to a postgresql:// keyword/value connection string.\n";
        return 2;
    }

    orm::Database database;
    database.connect(orm::db::BackendType::Postgres, connectionString);

    const auto& capabilities = database.getBackendCapabilities();
    std::cout << "Connected to PostgreSQL; transactions=" << std::boolalpha << capabilities.transactions
              << ", projections=" << capabilities.query.projections
              << ", collection-relations=" << capabilities.relations.manyToMany << '\n';

    database.disconnect();
    return 0;
}
