#include "tests/StandardLibrary.hpp"

import orm.reflection;

struct NonAggregate
{
    explicit NonAggregate(int input) : value(input) {}

    int value;
};

constexpr auto count = orm::reflection::fieldCount<NonAggregate>;
