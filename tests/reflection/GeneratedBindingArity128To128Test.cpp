#include "tests/reflection/GeneratedBindingRuntimeTest.hpp"

TEST(ReflectionGeneratedBindingsTest, arities128To128)
{
    reflection_binding_tests::verifyRange<128>(std::make_index_sequence<1>{});
}
