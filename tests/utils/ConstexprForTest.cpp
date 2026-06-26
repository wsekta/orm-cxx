#include "orm-cxx/utils/ConstexprFor.hpp"

#include <gtest/gtest.h>

#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

using namespace orm::utils;
using namespace std::string_literals;

TEST(ConstexprForTest, shouldIterateOverRange)
{
    auto indexes = std::vector<int>{};

    constexpr_for<0, 3, 1>([&indexes](auto index) { indexes.push_back(index); });

    EXPECT_EQ(indexes, std::vector<int>({0, 1, 2}));
}

TEST(ConstexprForTest, shouldNotIterateOverEmptyRange)
{
    auto wasCalled = false;

    constexpr_for<3, 3, 1>([&wasCalled](auto) { wasCalled = true; });

    EXPECT_FALSE(wasCalled);
}

TEST(ConstexprForTest, shouldIterateOverTupleValues)
{
    auto tuple = std::make_tuple(42, "value"s);
    auto indexes = std::vector<std::size_t>{};

    constexpr_for_tuple(tuple,
                        [&indexes](auto index, const auto& value)
                        {
                            constexpr auto indexValue = decltype(index)::value;
                            indexes.push_back(indexValue);

                            if constexpr (indexValue == 0)
                            {
                                EXPECT_EQ(value, 42);
                            }
                            else
                            {
                                EXPECT_EQ(value, "value"s);
                            }
                        });

    EXPECT_EQ(indexes, std::vector<std::size_t>({0, 1}));
}

TEST(ConstexprForTest, shouldIterateOverTupleTypes)
{
    using tuple_t = std::tuple<int*, const double*>;

    auto indexes = std::vector<std::size_t>{};

    constexpr_for_tuple<tuple_t>(
        [&indexes](auto index, auto field)
        {
            constexpr auto indexValue = decltype(index)::value;
            indexes.push_back(indexValue);
            EXPECT_EQ(field, nullptr);

            if constexpr (indexValue == 0)
            {
                static_assert(std::is_same_v<decltype(field), int*>);
            }
            else
            {
                static_assert(std::is_same_v<decltype(field), const double*>);
            }
        });

    EXPECT_EQ(indexes, std::vector<std::size_t>({0, 1}));
}
