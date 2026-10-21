#include "tests/StandardLibrary.hpp"

import orm;

auto context = orm::Database{orm::SqliteConfig{.database = orm::SqliteMemory{}}}.orm<orm::Schema<>>();
