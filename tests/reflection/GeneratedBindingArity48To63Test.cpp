#include "tests/reflection/GeneratedBindingRuntimeTest.hpp"

TEST(ReflectionGeneratedBindingsTest, arities48To63)
{
    reflection_binding_tests::verifyRange<48>(std::make_index_sequence<16>{});
}
