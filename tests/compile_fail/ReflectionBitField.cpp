#include "tests/StandardLibrary.hpp"

import orm.reflection;

struct BitField
{
    unsigned int enabled : 1;
};

constexpr auto descriptors = orm::reflection::fields<BitField>();
