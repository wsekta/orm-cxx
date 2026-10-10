#include "tests/StandardLibrary.hpp"

import orm;

auto context = orm::Database{}.orm<orm::Schema<>>();
