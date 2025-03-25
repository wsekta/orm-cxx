#include "orm-cxx/utils/ConstexprStringView.hpp"

#include <gtest/gtest.h>

using namespace orm::utils;
using namespace std::string_view_literals;
using namespace std::string_literals;

TEST(ConstexprStringViewTest, shouldReturnStringView)
{
    EXPECT_EQ(makeConstexprStringView([]() { return "test"; }), "test"sv);

    static_assert(makeConstexprStringView([]() { return "test"; }) == "test"sv);

    EXPECT_EQ(makeConstexprStringView([]() { return "test"s; }), "test"sv);

    static_assert(makeConstexprStringView([]() { return "test"s; }) == "test"sv);
}
