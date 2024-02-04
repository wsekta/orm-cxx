#pragma once

#include <string>

namespace orm::query
{
class Condition
{
    enum class Operator
    {
        EQUAL,
        NOT_EQUAL,
        GREATER,
        GREATER_OR_EQUAL,
        LESS,
        LESS_OR_EQUAL,
        LIKE,
        NOT_LIKE,
        IS_NULL,
        IS_NOT_NULL
    };

public:
    Condition(std::string columnName);

    //    auto operator==(const std::string& value) -> Condition&;
    //
    //    auto operator!=(const std::string& value) -> Condition&;
    //
    //    auto operator>(const std::string& value) -> Condition&;
    //
    //    auto operator>=(const std::string& value) -> Condition&;
    //
    //    auto operator<(const std::string& value) -> Condition&;
    //
    //    auto operator<=(const std::string& value) -> Condition&;

    auto like(const std::string& value) -> Condition&;

    auto notLike(const std::string& value) -> Condition&;

    auto isNull() -> Condition&;

    auto isNotNull() -> Condition&;

private:
    std::string columnName;

    Operator operatorType;

    std::string comparisonValue;
};
} // namespace orm::query
