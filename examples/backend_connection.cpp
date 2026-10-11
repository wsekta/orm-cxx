#include <iostream>

import orm;

int main(int argc, char** argv) // NOLINT(bugprone-exception-escape)
{
    if (argc != 2)
    {
        std::cerr << "Pass a connection string for an explicitly enabled backend.\n";
        return 2;
    }
    orm::Database database;
    database.connect(argv[1]);
    const auto& capabilities = database.getBackendCapabilities();
    std::cout << "Connected; transactions=" << std::boolalpha << capabilities.transactions
              << ", projections=" << capabilities.query.projections
              << ", collection-relations=" << capabilities.relations.manyToMany << '\n';
    database.disconnect();
    return 0;
}
