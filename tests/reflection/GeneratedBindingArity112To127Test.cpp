#include "tests/reflection/GeneratedBindingRuntimeTest.hpp"

TEST(ReflectionGeneratedBindingsTest, arities112To127)
{
    reflection_binding_tests::verifyRange<112>(std::make_index_sequence<16>{});
}
