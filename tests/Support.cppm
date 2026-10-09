module;

#include "tests/UnitTestPrelude.hpp"

module orm:test_support;

import orm.reflection;
import :internal;
import :foundation;
import :model;
import :expressions;
import :dynamic_query;
import :static_plan;
import :sql;
import :database;

#include "tests/CollectionModelsDefinitions.hpp"
#include "tests/compile_fail/StaticPlanModels.hpp"
#include "tests/compile_fail/TypedQueryModels.hpp"
#include "tests/database/DatabaseTest.hpp"
#include "tests/ModelsDefinitions.hpp"
#include "tests/utils/FakeDatabase.hpp"
#include "tests/utils/GenerateModels.hpp"
#include "tests/utils/RuntimeQueryBuilder.hpp"
#include "tests/utils/SqlDialectTestDoubles.hpp"
