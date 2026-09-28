#include "tests/reflection/GeneratedBindingRuntimeTest.hpp"

TEST(ReflectionGeneratedBindingsTest, arities64To79)
{
    reflection_binding_tests::verifyRange<64>(std::make_index_sequence<16>{});
}
