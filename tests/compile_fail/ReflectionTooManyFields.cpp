#include "tests/StandardLibrary.hpp"

import orm.reflection;

#include "../reflection/GeneratedFieldLimitModels.hpp"

static_assert(orm::reflection::fieldCount<reflection_limit_models::TooManyFields> == 129);
