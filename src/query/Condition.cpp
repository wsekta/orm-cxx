#include "orm-cxx/query/Condition.hpp"

namespace orm::query
{
Condition::Condition(std::string columnNameInit)
    : columnName{std::move(columnNameInit)}, operatorType{Operator::NO_OPERATOR}
{
}

auto Condition::like(const std::string& value) -> Condition&
{
    operatorType = Operator::LIKE;

    comparisonValue = value;

    return *this;
}

auto Condition::notLike(const std::string& value) -> Condition&
{
    operatorType = Operator::NOT_LIKE;

    comparisonValue = value;

    return *this;
}

auto Condition::isNull() -> Condition&
{
    operatorType = Operator::IS_NULL;

    return *this;
}

auto Condition::isNotNull() -> Condition&
{
    operatorType = Operator::IS_NOT_NULL;

    return *this;
}

auto Condition::getColumnName() const -> const std::string&
{
    return columnName;
}

auto Condition::getOperatorType() const -> Operator
{
    return operatorType;
}

auto Condition::getComparisonValue() const -> const std::string&
{
    return comparisonValue;
}
} // namespace orm::query
