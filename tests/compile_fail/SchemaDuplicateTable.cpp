#include "orm-cxx/model/Schema.hpp"

struct FirstModel
{
    int id;
    inline static constexpr orm::reflection::FixedString table_name{"duplicate_table"};
};

struct SecondModel
{
    int id;
    inline static constexpr orm::reflection::FixedString table_name{"duplicate_table"};
};

using BadSchema = orm::Schema<FirstModel, SecondModel>;
constexpr auto badSchema = BadSchema::view;
