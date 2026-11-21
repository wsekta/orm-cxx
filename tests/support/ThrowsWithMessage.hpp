#pragma once

#include <exception>
#include <string_view>
#include <utility>

#include "gtest/gtest.h"

namespace orm::test
{
// A direct exception check avoids Clang 18's false positives when analyzing
// GoogleMock's nested ThrowsMessage matchers through imported modules.
template <typename Exception, typename Action>
auto throwsWithMessage(Action&& action, std::string_view expected) -> ::testing::AssertionResult
{
    try
    {
        std::forward<Action>(action)();
    }
    catch (const Exception& error)
    {
        if (std::string_view{error.what()} == expected)
            return ::testing::AssertionSuccess();
        return ::testing::AssertionFailure()
               << "Expected exception message: " << expected << "\nActual: " << error.what();
    }
    catch (const std::exception& error)
    {
        return ::testing::AssertionFailure() << "Unexpected exception type: " << error.what();
    }
    catch (...)
    {
        return ::testing::AssertionFailure() << "Unexpected non-standard exception";
    }
    return ::testing::AssertionFailure() << "No exception was thrown; expected message: " << expected;
}
} // namespace orm::test
