#include <string_view>

#include "orm-cxx/reflection/Reflection.hpp"

struct InstalledModel
{
    int id;
    double score;
};

static_assert(orm::reflection::fieldCount<InstalledModel> == 2);
static_assert(orm::reflection::fieldName<InstalledModel, 0>() == "id");
static_assert(orm::reflection::getTypeName<InstalledModel>().ends_with("InstalledModel"));

int main()
{
    InstalledModel value{1, 2.0};
    auto fields = orm::reflection::tieFields(value);
    std::get<0>(fields) = 3;
    return value.id == 3 ? 0 : 1;
}
