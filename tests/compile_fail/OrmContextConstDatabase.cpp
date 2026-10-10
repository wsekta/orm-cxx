#include "tests/StandardLibrary.hpp"

import orm;

const orm::Database database;
auto context = database.orm<orm::Schema<>>();
