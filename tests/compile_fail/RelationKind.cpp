#include "tests/StandardLibrary.hpp"

import orm;

struct Owner
{
    int value;
};

constexpr auto invalid = orm::oneToMany<&Owner::value>();
