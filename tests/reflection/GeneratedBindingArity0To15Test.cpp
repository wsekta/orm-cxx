#include "tests/reflection/GeneratedBindingRuntimeTest.hpp"

TEST(ReflectionGeneratedBindingsTest, arities0To15)
{
    reflection_binding_tests::verifyRange<0>(std::make_index_sequence<16>{});
}
