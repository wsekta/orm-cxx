#include <optional>

#include "orm-cxx/model/Schema.hpp"

struct NullablePrimaryKey
{
    std::optional<int> id;

    inline static constexpr auto id_columns = orm::primaryKey<&NullablePrimaryKey::id>();
};

using BadSchema = orm::Schema<NullablePrimaryKey>;
constexpr auto badSchema = BadSchema::view;
