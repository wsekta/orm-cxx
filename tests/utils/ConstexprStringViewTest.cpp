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

TEST(ConstexprStringViewTest, shouldMakeOversizedArray)
{
#if defined(_MSC_VER)
    static_assert(
        []
        {
            constexpr auto result = makeOversizedArray("abc"s);

            return result.size == 3 and result.data[0] == 'a' and result.data[1] == 'b'
                   and result.data[2] == 'c';
        }());
#else
    auto result = makeOversizedArray("abc"s);

    EXPECT_EQ(result.size, 3);
    EXPECT_EQ(std::string(result.data.data(), result.size), "abc"s);
#endif
}

TEST(ConstexprStringViewTest, shouldReturnEmptyStringView)
{
    EXPECT_EQ(makeConstexprStringView([]() { return ""; }), ""sv);

    static_assert(makeConstexprStringView([]() { return ""s; }).empty());
}

TEST(ConstexprStringViewTest, shouldReturnLongerStringView)
{
    EXPECT_EQ(makeConstexprStringView([]() { return "longer test value"s; }), "longer test value"sv);

    static_assert(makeConstexprStringView([]() { return "longer test value"s; }) == "longer test value"sv);
}

TEST(ConstexprStringViewTest, shouldReturnStringViewWithSpaces)
{
    EXPECT_EQ(makeConstexprStringView([]() { return " value with spaces "s; }), " value with spaces "sv);

    static_assert(makeConstexprStringView([]() { return " value with spaces "s; }) == " value with spaces "sv);
}

TEST(ConstexprStringViewTest, shouldAcceptStringCallable)
{
    static_assert(StringCallable<decltype([]() { return "test"; })>);
    static_assert(StringCallable<decltype([]() { return "test"s; })>);
}
