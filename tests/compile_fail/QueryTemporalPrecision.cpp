#include "tests/StandardLibrary.hpp"

import orm;

#include "TemporalQueryModels.hpp"

using namespace orm::query;
using namespace temporal_query_models;

[[maybe_unused]] auto rejected() -> void
{
    (void)(col<&Record::instant>() == std::chrono::sys_time<std::chrono::nanoseconds>{});
}
