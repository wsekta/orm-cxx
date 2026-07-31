#include "orm-cxx/reflection/Reflection.hpp"

struct BitField
{
    unsigned int enabled : 1;
};

constexpr auto descriptors = orm::reflection::fields<BitField>();
