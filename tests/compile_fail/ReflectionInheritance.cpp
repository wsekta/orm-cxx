#include "tests/StandardLibrary.hpp"

import orm.reflection;

struct Base
{
    int base;
};

struct Derived : Base
{
    int own;
};

constexpr auto descriptors = orm::reflection::fields<Derived>();
