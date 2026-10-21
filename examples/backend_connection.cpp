#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

import orm;

#include "Connection.hpp"

int main(int argc, char** argv) // NOLINT(bugprone-exception-escape)
{
    auto database = makeExampleDatabase(argc, argv);

    const auto& capabilities = database.getBackendCapabilities();
    std::cout << "Connected; transactions=" << std::boolalpha << capabilities.transactions
              << ", projections=" << capabilities.query.projections
              << ", collection-relations=" << capabilities.relations.manyToMany << '\n';
    database.disconnect();
    return 0;
}
