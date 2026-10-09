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

TEST(ReflectionGeneratedBindingsTest, arities0To15)
{
    reflection_binding_tests::verifyRange<0>(std::make_index_sequence<16>{});
}
