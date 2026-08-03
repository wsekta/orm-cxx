#include "orm-cxx/reflection/Reflection.hpp"

struct NonAggregate
{
    explicit NonAggregate(int input) : value(input) {}

    int value;
};

constexpr auto count = orm::reflection::fieldCount<NonAggregate>;
