#include "tests/reflection/GeneratedBindingRuntimeTest.hpp"

TEST(ReflectionGeneratedBindingsTest, arities80To95)
{
    reflection_binding_tests::verifyRange<80>(std::make_index_sequence<16>{});
}
