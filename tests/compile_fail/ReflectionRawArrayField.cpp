#include "tests/StandardLibrary.hpp"

import orm.reflection;

struct RawArrayField
{
    int values[2];
};

constexpr auto descriptors = orm::reflection::fields<RawArrayField>();
