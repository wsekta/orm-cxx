#include <vector>

#include "TypedQueryModels.hpp"

using namespace orm::query;
using namespace typed_query_models;

struct Unsupported
{
    int id;
    std::vector<int> values;
};

[[maybe_unused]] auto rejected(Database& database) -> void
{
    (void)database;
    (void)col<&Unsupported::values>();
}
