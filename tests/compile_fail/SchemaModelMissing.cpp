#include "orm-cxx/model/Schema.hpp"

struct Included
{
    int id;
};

struct Missing
{
    int id;
};

using AppSchema = orm::Schema<Included>;
constexpr auto requireMissing = []() consteval
{
    orm::model::requireSchemaModel<AppSchema, Missing>();
    return true;
}();
