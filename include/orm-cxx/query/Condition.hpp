#pragma once

#include <string>

#include "Operator.hpp"

namespace orm::query
{
class Condition
{
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

    auto getColumnName() const -> const std::string&;

    auto getOperatorType() const -> Operator;

    auto getComparisonValue() const -> const std::string&;

private:
    std::string columnName;

    Operator operatorType;

    std::string comparisonValue;
};
} // namespace orm::query
