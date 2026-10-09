#include "tests/StandardLibrary.hpp"

import orm;

#include "TypedQueryModels.hpp"

using namespace orm::query;
using namespace typed_query_models;

auto function() -> int
{
    return 1;
}

[[maybe_unused]] auto rejected(Database& database) -> void
{
    (void)database;
    (void)col<&function>();
}
