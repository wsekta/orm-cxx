#include "orm-cxx/relations.hpp"

struct Owner
{
    int value;
};

constexpr auto invalid = orm::oneToMany<&Owner::value>();
