#include "orm-cxx/reflection/Reflection.hpp"

struct Base
{
    int base;
};

struct Derived : Base
{
    int own;
};

constexpr auto descriptors = orm::reflection::fields<Derived>();
