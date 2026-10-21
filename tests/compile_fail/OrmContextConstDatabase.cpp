#include "tests/StandardLibrary.hpp"

import orm;

const orm::Database database{orm::SqliteConfig{.database = orm::SqliteMemory{}}};
auto context = database.orm<orm::Schema<>>();
