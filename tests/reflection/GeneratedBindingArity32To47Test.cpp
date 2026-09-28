#include "tests/reflection/GeneratedBindingRuntimeTest.hpp"

TEST(ReflectionGeneratedBindingsTest, arities32To47)
{
    reflection_binding_tests::verifyRange<32>(std::make_index_sequence<16>{});
}
