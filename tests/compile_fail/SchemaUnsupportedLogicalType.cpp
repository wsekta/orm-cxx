#include "orm-cxx/model/Schema.hpp"

struct Unsupported
{
};

struct Model
{
    int id;
    Unsupported value;
};

using BadSchema = orm::Schema<Model>;
constexpr auto badSchema = BadSchema::view;
