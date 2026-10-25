#include "tests/StandardLibrary.hpp"

import orm;

#include "TemporalQueryModels.hpp"

using namespace orm::query;
using namespace temporal_query_models;

[[maybe_unused]] auto rejected() -> void
{
    (void)(col<&Record::local>() == std::string{"2024-02-29 12:34:56.123456"});
}
