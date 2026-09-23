#include <orm-cxx/reflection/Reflection.hpp>

struct ReflectedEntry
{
    int id;
};

int main()
{
    static_assert(orm::reflection::fieldCount<ReflectedEntry> == 1);
    static_assert(orm::reflection::fieldName<ReflectedEntry, 0>() == "id");
    return 0;
}
