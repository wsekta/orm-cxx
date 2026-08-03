#include "orm-cxx/model/Schema.hpp"

struct Model
{
    int id;
    inline static constexpr int auto_increment_columns = 7;
};

using BadSchema = orm::Schema<Model>;
constexpr auto badSchema = BadSchema::view;
