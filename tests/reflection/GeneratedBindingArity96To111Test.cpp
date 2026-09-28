#include "tests/reflection/GeneratedBindingRuntimeTest.hpp"

TEST(ReflectionGeneratedBindingsTest, arities96To111)
{
    reflection_binding_tests::verifyRange<96>(std::make_index_sequence<16>{});
}
