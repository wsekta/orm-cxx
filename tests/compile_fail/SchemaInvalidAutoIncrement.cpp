#include <string>

#include "orm-cxx/model/Schema.hpp"

struct InvalidAutoIncrement
{
    int id;
    std::string value;

    inline static constexpr auto id_columns = orm::primaryKey<&InvalidAutoIncrement::value>();
    inline static constexpr auto auto_increment_columns = orm::autoIncrement<&InvalidAutoIncrement::value>();
};

using BadSchema = orm::Schema<InvalidAutoIncrement>;
constexpr auto badSchema = BadSchema::view;
