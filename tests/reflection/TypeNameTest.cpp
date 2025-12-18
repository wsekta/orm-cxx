#include "orm-cxx/reflection/TypeName.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace orm::reflection;
using namespace std::string_view_literals;

TEST(TypeNameTest, basicTypesNames)
{
    EXPECT_EQ(getTypeName<int>(), "int"sv);
    EXPECT_EQ(getTypeName<float>(), "float"sv);
    EXPECT_EQ(getTypeName<double>(), "double"sv);
}

TEST(TypeNameTest, structs)
{
    struct TestStruct
    {
        int a;
    };

    EXPECT_THAT(getTypeName<TestStruct>(), ::testing::HasSubstr("TestStruct"));
}

TEST(TypeNameTest, classes)
{
    class TestClass
    {
        int a;
    };

    EXPECT_THAT(getTypeName<TestClass>(), ::testing::HasSubstr("TestClass"));
}
