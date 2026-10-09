#include "tests/StandardLibrary.hpp"

import orm.reflection;

union UnsupportedUnion
{
    int integer;
    double real;
};

static_assert(orm::reflection::fieldCount<UnsupportedUnion> == 2);
