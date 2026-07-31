#include "orm-cxx/model/Schema.hpp"

struct DuplicateModel
{
    int id;
};

using BadSchema = orm::Schema<DuplicateModel, DuplicateModel>;
constexpr auto badSchema = BadSchema::view;
