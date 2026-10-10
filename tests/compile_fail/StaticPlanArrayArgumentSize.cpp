#include "tests/StandardLibrary.hpp"

import orm;

#include "TypedQueryModels.hpp"

using namespace orm::query;
using namespace typed_query_models;

[[maybe_unused]] auto rejected(Context& database) -> void
{
    (void)database;
    auto plan = select<User>().where(col<&User::age>().in(param<std::array<int, 2>, 0>()));
    (void)plan.toDynamic(std::array{18, 21, 30});
}
