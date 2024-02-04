#include "orm-cxx/query/Condition.hpp"

namespace orm::query
{
Condition::Condition(std::string columnName) : columnName{std::move(columnName)} {}

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
} // namespace orm::query
