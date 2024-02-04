#pragma once

#include <map>

namespace orm::query
{
enum class Operator
{
    NO_OPERATOR,
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
} // namespace orm::query
