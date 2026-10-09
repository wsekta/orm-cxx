module;

#include <array>
#include <cstddef>
#include <gtest/gtest.h>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

#include "tests/reflection/GeneratedFieldLimitModels.hpp"

module orm.reflection;

import :generated;

#include "tests/reflection/GeneratedBindingRuntimeTest.hpp"

TEST(ReflectionGeneratedBindingsTest, arities128To128)
{
    reflection_binding_tests::verifyRange<128>(std::make_index_sequence<1>{});
}
