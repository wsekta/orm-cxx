#include "orm-cxx/reflection/Reflection.hpp"

struct RawArrayField
{
    int values[2];
};

constexpr auto descriptors = orm::reflection::fields<RawArrayField>();
