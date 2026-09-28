#include "tests/reflection/GeneratedBindingRuntimeTest.hpp"

TEST(ReflectionGeneratedBindingsTest, arities16To31)
{
    reflection_binding_tests::verifyRange<16>(std::make_index_sequence<16>{});
}
