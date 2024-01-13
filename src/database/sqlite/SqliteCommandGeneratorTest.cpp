#include "SqliteCommandGenerator.hpp"

#include <gtest/gtest.h>

using namespace orm::db::sqlite;

class SqliteCommandGeneratorTest : public ::testing::Test
{
public:
    SqliteCommandGenerator generator;
};

// TODO: implement tests