#include "tests/StandardLibrary.hpp"

import orm;

#include "TemporalQueryModels.hpp"

using namespace orm::query;
using namespace temporal_query_models;

[[maybe_unused]] auto rejected() -> void
{
    (void)asc(col<&Record::uuid>());
}
