#include "orm-cxx/reflection/Reflection.hpp"

union UnsupportedUnion
{
    int integer;
    double real;
};

static_assert(orm::reflection::fieldCount<UnsupportedUnion> == 2);
